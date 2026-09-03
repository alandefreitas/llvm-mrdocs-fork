//===- Legality.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Legality checks for the Sandbox Vectorizer.
//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_LEGALITY_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_LEGALITY_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Vectorize/SandboxVectorizer/InstrMaps.h"
#include "llvm/Transforms/Vectorize/SandboxVectorizer/Scheduler.h"

namespace llvm::sandboxir {

class LegalityAnalysis;
class Value;
class InstrMaps;

/// A shuffle mask describing how to rearrange vector lanes.
class ShuffleMask {
public:
  /// Storage type for shuffle mask indices.
  using IndicesVecT = SmallVector<int, 8>;

private:
  IndicesVecT Indices;

public:
  /// Construct a shuffle mask by taking ownership of \p Indices.
  /// @param Indices Mask indices moved into this object.
  ShuffleMask(SmallVectorImpl<int> &&Indices) : Indices(std::move(Indices)) {}
  /// Construct a shuffle mask from an initializer list of indices.
  /// @param Indices Initializer list of mask indices.
  ShuffleMask(std::initializer_list<int> Indices) : Indices(Indices) {}
  /// Construct a shuffle mask from an ArrayRef of indices.
  /// @param Indices Array of mask indices to copy.
  explicit ShuffleMask(ArrayRef<int> Indices) : Indices(Indices) {}
  /// Convert this mask to an ArrayRef of indices.
  /// @return An ArrayRef view of the mask indices.
  operator ArrayRef<int>() const { return Indices; }
  /// Creates and returns an identity shuffle mask of size \p Sz.
  ///
  /// For example if Sz == 4 the returned mask is {0, 1, 2, 3}.
  /// @param Sz Number of lanes in the identity mask.
  /// @return An identity shuffle mask of size \p Sz.
  static ShuffleMask getIdentity(unsigned Sz) {
    IndicesVecT Indices;
    Indices.reserve(Sz);
    llvm::append_range(Indices, seq<int>(0, (int)Sz));
    return ShuffleMask(std::move(Indices));
  }
  /// Return true if the mask is a perfect identity mask.
  ///
  /// True when the mask has consecutive indices and performs no lane
  /// shuffling, like 0,1,2,3...
  /// @return True if the mask is an identity mask.
  bool isIdentity() const {
    for (auto [Idx, Elm] : enumerate(Indices)) {
      if ((int)Idx != Elm)
        return false;
    }
    return true;
  }
  /// Return true if this mask equals \p Other.
  /// @param Other Shuffle mask to compare against.
  /// @return True if both masks have the same indices.
  bool operator==(const ShuffleMask &Other) const {
    return Indices == Other.Indices;
  }
  /// Return true if this mask differs from \p Other.
  /// @param Other Shuffle mask to compare against.
  /// @return True if the masks differ.
  bool operator!=(const ShuffleMask &Other) const { return !(*this == Other); }
  /// Return the number of indices in the mask.
  /// @return The number of mask indices.
  size_t size() const { return Indices.size(); }
  /// Return the mask index at position \p Idx.
  /// @param Idx Lane index to look up.
  /// @return The mask index at \p Idx.
  int operator[](int Idx) const { return Indices[Idx]; }
  /// Const iterator over mask indices.
  using const_iterator = IndicesVecT::const_iterator;
  /// Return an iterator to the first mask index.
  /// @return A const iterator to the first index.
  const_iterator begin() const { return Indices.begin(); }
  /// Return an iterator past the last mask index.
  /// @return A const iterator past the last index.
  const_iterator end() const { return Indices.end(); }
#ifndef NDEBUG
  /// Write \p Mask to \p OS.
  /// @param OS Stream to write to.
  /// @param Mask Shuffle mask to print.
  /// @return The output stream \p OS.
  friend raw_ostream &operator<<(raw_ostream &OS, const ShuffleMask &Mask) {
    Mask.print(OS);
    return OS;
  }
  /// Print this mask to \p OS.
  /// @param OS Stream to write to.
  void print(raw_ostream &OS) const {
    interleave(Indices, OS, [&OS](auto Elm) { OS << Elm; }, ",");
  }
  /// Dump this mask to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif
};

/// Identifiers for concrete LegalityResult subclasses.
enum class LegalityResultID {
  /// Collect scalar values.
  Pack,
  /// Vectorize by combining scalars to a vector.
  Widen,
  /// Don't generate new code, reuse existing vector.
  DiamondReuse,
  /// Reuse the existing vector but add a shuffle.
  DiamondReuseWithShuffle,
  /// Reuse more than one vector and/or scalars.
  DiamondReuseMultiInput,
};

/// The reason for vectorizing or not vectorizing.
enum class ResultReason {
  /// Bundle elements are not all instructions.
  NotInstructions,
  /// Bundle instructions have different opcodes.
  DiffOpcodes,
  /// Bundle instructions have different types.
  DiffTypes,
  /// Bundle instructions have different fast-math flags.
  DiffMathFlags,
  /// Bundle instructions have different wrap flags.
  DiffWrapFlags,
  /// Bundle instructions are in different basic blocks.
  DiffBBs,
  /// Bundle contains the same instruction more than once.
  RepeatedInstrs,
  /// Memory accesses are not consecutive.
  NotConsecutive,
  /// The scheduler could not schedule the bundle together.
  CantSchedule,
  /// The legality check for this case is not implemented.
  Unimplemented,
  /// Vectorization is not feasible for this bundle.
  Infeasible,
  /// Forced pack result used only for debugging.
  ForcePackForDebugging,
};

#ifndef NDEBUG
/// Helpers that map legality enums to debug string names.
struct ToStr {
  /// Return the debug name for legality result ID \p ID.
  /// @param ID Legality result subclass identifier.
  /// @return A string name for \p ID.
  static const char *getLegalityResultID(LegalityResultID ID) {
    switch (ID) {
    case LegalityResultID::Pack:
      return "Pack";
    case LegalityResultID::Widen:
      return "Widen";
    case LegalityResultID::DiamondReuse:
      return "DiamondReuse";
    case LegalityResultID::DiamondReuseWithShuffle:
      return "DiamondReuseWithShuffle";
    case LegalityResultID::DiamondReuseMultiInput:
      return "DiamondReuseMultiInput";
    }
    llvm_unreachable("Unknown LegalityResultID enum");
  }

  /// Return the debug name for result reason \p Reason.
  /// @param Reason Reason why vectorization succeeded or failed.
  /// @return A string name for \p Reason.
  static const char *getVecReason(ResultReason Reason) {
    switch (Reason) {
    case ResultReason::NotInstructions:
      return "NotInstructions";
    case ResultReason::DiffOpcodes:
      return "DiffOpcodes";
    case ResultReason::DiffTypes:
      return "DiffTypes";
    case ResultReason::DiffMathFlags:
      return "DiffMathFlags";
    case ResultReason::DiffWrapFlags:
      return "DiffWrapFlags";
    case ResultReason::DiffBBs:
      return "DiffBBs";
    case ResultReason::RepeatedInstrs:
      return "RepeatedInstrs";
    case ResultReason::NotConsecutive:
      return "NotConsecutive";
    case ResultReason::CantSchedule:
      return "CantSchedule";
    case ResultReason::Unimplemented:
      return "Unimplemented";
    case ResultReason::Infeasible:
      return "Infeasible";
    case ResultReason::ForcePackForDebugging:
      return "ForcePackForDebugging";
    }
    llvm_unreachable("Unknown ResultReason enum");
  }
};
#endif // NDEBUG

/// Result of a legality check for vectorizing a bundle.
///
/// The legality outcome is represented by a class rather than an enum class
/// because in some cases the legality checks are expensive and look for a
/// particular instruction that can be passed along to the vectorizer to avoid
/// repeating the same expensive computation.
class LegalityResult {
protected:
  /// Subclass identifier for this legality result.
  LegalityResultID ID;
  /// Only Legality can create LegalityResults.
  /// @param ID Subclass identifier for the result being constructed.
  LegalityResult(LegalityResultID ID) : ID(ID) {}
  friend class LegalityAnalysis;

  /// We shouldn't need copies.
  /// @param Other Unused source object; copying is deleted.
  LegalityResult(const LegalityResult &Other) = delete;
  /// Deleted copy assignment; LegalityResult objects are not copyable.
  /// @param Other Unused source object; assignment is deleted.
  LegalityResult &operator=(const LegalityResult &Other) = delete;

public:
  /// Destroy this legality result.
  virtual ~LegalityResult() = default;
  /// Return the subclass identifier for this result.
  /// @return The LegalityResultID for this concrete result.
  LegalityResultID getSubclassID() const { return ID; }
#ifndef NDEBUG
  /// Print this result to \p OS.
  /// @param OS Stream to write to.
  virtual void print(raw_ostream &OS) const {
    OS << ToStr::getLegalityResultID(ID);
  }
  /// Dump this result to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
  /// Write \p LR to \p OS.
  /// @param OS Stream to write to.
  /// @param LR Legality result to print.
  /// @return The output stream \p OS.
  friend raw_ostream &operator<<(raw_ostream &OS, const LegalityResult &LR) {
    LR.print(OS);
    return OS;
  }
#endif // NDEBUG
};

/// Base class for results with reason.
class LegalityResultWithReason : public LegalityResult {
  ResultReason Reason;
  LegalityResultWithReason(LegalityResultID ID, ResultReason Reason)
      : LegalityResult(ID), Reason(Reason) {}
  friend class Pack; // For constructor.

public:
  /// Return the reason associated with this result.
  /// @return The ResultReason explaining this legality outcome.
  ResultReason getReason() const { return Reason; }
#ifndef NDEBUG
  /// Print this result and its reason to \p OS.
  /// @param OS Stream to write to.
  void print(raw_ostream &OS) const override {
    LegalityResult::print(OS);
    OS << " Reason: " << ToStr::getVecReason(Reason);
  }
#endif
};

/// Legality result indicating the bundle should be widened into a vector.
class Widen final : public LegalityResult {
  friend class LegalityAnalysis;
  Widen() : LegalityResult(LegalityResultID::Widen) {}

public:
  /// Return true if \p From is a Widen result.
  /// @param From Legality result to test.
  /// @return True if \p From is a Widen.
  static bool classof(const LegalityResult *From) {
    return From->getSubclassID() == LegalityResultID::Widen;
  }
};

/// Legality result that reuses an existing vector without new code.
class DiamondReuse final : public LegalityResult {
  friend class LegalityAnalysis;
  Action *Vec;
  DiamondReuse(Action *Vec)
      : LegalityResult(LegalityResultID::DiamondReuse), Vec(Vec) {}

public:
  /// Return true if \p From is a DiamondReuse result.
  /// @param From Legality result to test.
  /// @return True if \p From is a DiamondReuse.
  static bool classof(const LegalityResult *From) {
    return From->getSubclassID() == LegalityResultID::DiamondReuse;
  }
  /// Return the existing vector action to reuse.
  /// @return The vector Action to reuse.
  Action *getVector() const { return Vec; }
};

/// Legality result that reuses an existing vector with a shuffle.
class DiamondReuseWithShuffle final : public LegalityResult {
  friend class LegalityAnalysis;
  Action *Vec;
  ShuffleMask Mask;
  DiamondReuseWithShuffle(Action *Vec, const ShuffleMask &Mask)
      : LegalityResult(LegalityResultID::DiamondReuseWithShuffle), Vec(Vec),
        Mask(Mask) {}

public:
  /// Return true if \p From is a DiamondReuseWithShuffle result.
  /// @param From Legality result to test.
  /// @return True if \p From is a DiamondReuseWithShuffle.
  static bool classof(const LegalityResult *From) {
    return From->getSubclassID() == LegalityResultID::DiamondReuseWithShuffle;
  }
  /// Return the existing vector action to reuse.
  /// @return The vector Action to reuse.
  Action *getVector() const { return Vec; }
  /// Return the shuffle mask to apply to the reused vector.
  /// @return The shuffle mask for the reused vector.
  const ShuffleMask &getMask() const { return Mask; }
};

/// Legality result that packs scalars instead of vectorizing.
class Pack final : public LegalityResultWithReason {
  Pack(ResultReason Reason)
      : LegalityResultWithReason(LegalityResultID::Pack, Reason) {}
  friend class LegalityAnalysis; // For constructor.

public:
  /// Return true if \p From is a Pack result.
  /// @param From Legality result to test.
  /// @return True if \p From is a Pack.
  static bool classof(const LegalityResult *From) {
    return From->getSubclassID() == LegalityResultID::Pack;
  }
};

/// Describes how to collect the values needed by each lane.
class CollectDescr {
public:
  /// Describes how to get a value element. If the value is a vector then it
  /// also provides the index to extract it from.
  class ExtractElementDescr {
    PointerUnion<Action *, Value *> V = nullptr;
    /// The index in `V` that the value can be extracted from.
    int ExtractIdx = 0;

  public:
    /// Construct a descriptor that extracts lane \p ExtractIdx from vector
    /// \p V.
    /// @param V Vector action to extract from.
    /// @param ExtractIdx Lane index to extract.
    ExtractElementDescr(Action *V, int ExtractIdx)
        : V(V), ExtractIdx(ExtractIdx) {}
    /// Construct a descriptor that uses scalar value \p V directly.
    /// @param V Scalar value for this lane.
    ExtractElementDescr(Value *V) : V(V) {}
    /// Return the vector action this lane is extracted from.
    /// @return The vector Action to extract from.
    Action *getValue() const { return cast<Action *>(V); }
    /// Return the scalar value for this lane.
    /// @return The scalar Value for this lane.
    Value *getScalar() const { return cast<Value *>(V); }
    /// Return true if this lane needs an extract from a vector.
    /// @return True if this lane is backed by a vector Action.
    bool needsExtract() const { return isa<Action *>(V); }
    /// Return the extract index within the vector action.
    /// @return The lane index to extract.
    int getExtractIdx() const { return ExtractIdx; }
  };

  /// Vector of per-lane extract-or-scalar descriptors.
  using DescrVecT = SmallVector<ExtractElementDescr, 4>;
  /// Descriptors describing how to obtain each lane's value.
  DescrVecT Descrs;

public:
  /// Construct a collect description from lane descriptors \p Descrs.
  /// @param Descrs Per-lane descriptors moved into this object.
  CollectDescr(SmallVectorImpl<ExtractElementDescr> &&Descrs)
      : Descrs(std::move(Descrs)) {}
  /// If all elements come from a single vector input, then return that vector
  /// and also the shuffle mask required to get them in order.
  /// @return The vector and shuffle mask, or nullopt if inputs differ.
  std::optional<std::pair<Action *, ShuffleMask>> getSingleInput() const {
    const auto &Descr0 = *Descrs.begin();
    if (!Descr0.needsExtract())
      return std::nullopt;
    auto *V0 = Descr0.getValue();
    ShuffleMask::IndicesVecT MaskIndices;
    MaskIndices.push_back(Descr0.getExtractIdx());
    for (const auto &Descr : drop_begin(Descrs)) {
      if (!Descr.needsExtract())
        return std::nullopt;
      if (Descr.getValue() != V0)
        return std::nullopt;
      MaskIndices.push_back(Descr.getExtractIdx());
    }
    return std::make_pair(V0, ShuffleMask(std::move(MaskIndices)));
  }
  /// Return true if any lane is obtained from a vector via extract.
  /// @return True if at least one lane needs an extract.
  bool hasVectorInputs() const {
    return any_of(Descrs, [](const auto &D) { return D.needsExtract(); });
  }
  /// Return the per-lane descriptors.
  /// @return The descriptors for each lane.
  const SmallVector<ExtractElementDescr, 4> &getDescrs() const {
    return Descrs;
  }
};

/// Legality result that reuses multiple vectors and/or scalars.
class DiamondReuseMultiInput final : public LegalityResult {
  friend class LegalityAnalysis;
  CollectDescr Descr;
  DiamondReuseMultiInput(CollectDescr &&Descr)
      : LegalityResult(LegalityResultID::DiamondReuseMultiInput),
        Descr(std::move(Descr)) {}

public:
  /// Return true if \p From is a DiamondReuseMultiInput result.
  /// @param From Legality result to test.
  /// @return True if \p From is a DiamondReuseMultiInput.
  static bool classof(const LegalityResult *From) {
    return From->getSubclassID() == LegalityResultID::DiamondReuseMultiInput;
  }
  /// Return how to collect values from multiple inputs.
  /// @return The CollectDescr describing multi-input collection.
  const CollectDescr &getCollectDescr() const { return Descr; }
};

/// Performs the legality analysis and returns a LegalityResult object.
class LegalityAnalysis {
  Scheduler Sched;
  /// Owns the legality result objects created by createLegalityResult().
  SmallVector<std::unique_ptr<LegalityResult>> ResultPool;
  /// Checks opcodes, types and other IR-specifics and returns a ResultReason
  /// object if not vectorizable, or nullptr otherwise.
  std::optional<ResultReason>
  notVectorizableBasedOnOpcodesAndTypes(ArrayRef<Value *> Bndl);

  ScalarEvolution &SE;
  const DataLayout &DL;
  InstrMaps &IMaps;

  /// Finds how we can collect the values in \p Bndl from the vectorized or
  /// non-vectorized code. It returns a map of the value we should extract from
  /// and the corresponding shuffle mask we need to use.
  CollectDescr getHowToCollectValues(ArrayRef<Value *> Bndl) const;

public:
  /// Construct a legality analysis for the given analyses and context.
  /// @param AA Alias analysis used by the scheduler.
  /// @param SE Scalar evolution analysis.
  /// @param DL Data layout of the module.
  /// @param Ctx Sandbox IR context.
  /// @param IMaps Maps between scalar and vectorized values.
  /// @param Dir Scheduling direction for the internal scheduler.
  LegalityAnalysis(AAResults &AA, ScalarEvolution &SE, const DataLayout &DL,
                   Context &Ctx, InstrMaps &IMaps, SchedDirection Dir)
      : Sched(AA, Ctx, Dir), SE(SE), DL(DL), IMaps(IMaps) {}
  /// A LegalityResult factory.
  /// @param Args Arguments forwarded to the ResultT constructor.
  /// @return A reference to the newly created ResultT owned by this analysis.
  template <typename ResultT, typename... ArgsT>
  ResultT &createLegalityResult(ArgsT &&...Args) {
    ResultPool.push_back(
        std::unique_ptr<ResultT>(new ResultT(std::move(Args)...)));
    return cast<ResultT>(*ResultPool.back());
  }

  /// Return true if \p Instrs are in different blocks.
  /// @param Instrs Values expected to be instructions whose parents are
  /// compared.
  /// @return True if any instruction is in a different block than the first.
  template <typename ValueT>
  static bool differentBlock(ArrayRef<ValueT *> Instrs) {
    auto *BB0 = cast<Instruction>(Instrs[0])->getParent();
    return any_of(drop_begin(Instrs), [BB0](auto *V) {
      return cast<Instruction>(V)->getParent() != BB0;
    });
  }

  /// Return true if all values in \p Values are unique.
  /// @param Values Values to check for uniqueness.
  /// @return True if every value in \p Values appears exactly once.
  template <typename ValueT> static bool areUnique(ArrayRef<ValueT *> Values) {
    SmallPtrSet<Value *, 8> Unique(llvm::from_range, Values);
    return Unique.size() == Values.size();
  }

  // TODO: Try to remove the SkipScheduling argument by refactoring the tests.
  /// Checks if it's legal to vectorize the instructions in \p Bndl.
  ///
  /// \p SkipScheduling skips the scheduler check and is only meant for testing.
  /// @param Bndl Bundle of values considered for vectorization.
  /// @param SkipScheduling When true, skip the scheduler legality check.
  /// @return A LegalityResult object owned by LegalityAnalysis.
  LLVM_ABI const LegalityResult &canVectorize(ArrayRef<Value *> Bndl,
                                              bool SkipScheduling = false);
  /// Return a Pack forced for debugging.
  /// @return A Pack with reason 'ForcePackForDebugging'.
  const LegalityResult &getForcedPackForDebugging() {
    return createLegalityResult<Pack>(ResultReason::ForcePackForDebugging);
  }
  /// Clear cached legality state.
  LLVM_ABI void clear();
};

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_LEGALITY_H
