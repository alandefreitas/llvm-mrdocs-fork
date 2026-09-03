//===- VecUtils.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Collector for SandboxVectorizer related convenience functions that don't
// belong in other classes.

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_VECUTILS_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_VECUTILS_H

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/SandboxIR/Type.h"
#include "llvm/SandboxIR/Utils.h"
#include "llvm/Support/Compiler.h"
#include <iterator>

namespace llvm {
/// Traits for DenseMap keyed by SmallVector of sandboxir::Value pointers.
template <> struct DenseMapInfo<SmallVector<sandboxir::Value *>> {
  /// Compute a hash value for \p Vec.
  /// \param Vec Vector of values to hash.
  /// \return A hash combining the elements of \p Vec.
  static unsigned getHashValue(const SmallVector<sandboxir::Value *> &Vec) {
    return hash_combine_range(Vec);
  }
  /// Return true if \p Vec1 and \p Vec2 compare equal.
  /// \param Vec1 Left-hand vector.
  /// \param Vec2 Right-hand vector.
  /// \return True if \p Vec1 and \p Vec2 contain the same elements.
  static bool isEqual(const SmallVector<sandboxir::Value *> &Vec1,
                      const SmallVector<sandboxir::Value *> &Vec2) {
    return Vec1 == Vec2;
  }
};

namespace sandboxir {

class InstrMaps;

/// Small vector of values representing a vectorization bundle.
using BundleTy = SmallVector<Value *, 4>;

/// Convenience helpers for the SandboxVectorizer that do not belong elsewhere.
class VecUtils {
public:
  /// Return the number of elements in \p Ty.
  ///
  /// That is the number of lanes if a fixed vector or 1 if scalar.
  /// ScalableVectors have unknown size and therefore are unsupported.
  /// \param Ty Type to query; must not be a scalable vector.
  /// \return The number of elements in \p Ty, or 1 if \p Ty is scalar.
  static int getNumElements(Type *Ty) {
    assert(!isa<ScalableVectorType>(Ty));
    return Ty->isVectorTy() ? cast<FixedVectorType>(Ty)->getNumElements() : 1;
  }
  /// Return \p Ty if scalar or its element type if vector.
  /// \param Ty Type to query.
  /// \return \p Ty if scalar, otherwise the element type of the vector.
  static Type *getElementType(Type *Ty) {
    return Ty->isVectorTy() ? cast<FixedVectorType>(Ty)->getElementType() : Ty;
  }

  /// Return true if \p I1 and \p I2 access consecutive memory.
  ///
  /// Both must be loads or stores.
  /// \param I1 First load or store.
  /// \param I2 Second load or store.
  /// \param SE Scalar evolution analysis used for pointer diffs.
  /// \param DL Data layout used for size calculations.
  /// \return True if \p I1 and \p I2 access consecutive memory locations.
  template <typename LoadOrStoreT>
  static bool areConsecutive(LoadOrStoreT *I1, LoadOrStoreT *I2,
                             ScalarEvolution &SE, const DataLayout &DL) {
    static_assert(std::is_same<LoadOrStoreT, LoadInst>::value ||
                      std::is_same<LoadOrStoreT, StoreInst>::value,
                  "Expected Load or Store!");
    auto Diff = Utils::getPointerDiffInBytes(I1, I2, SE);
    if (!Diff)
      return false;
    int ElmBytes = Utils::getNumBits(I1) / 8;
    return *Diff == ElmBytes;
  }

  /// Return true if all loads/stores in \p Bndl access consecutive memory.
  /// \param Bndl Bundle of load or store instructions to check.
  /// \param SE Scalar evolution analysis used for pointer diffs.
  /// \param DL Data layout used for size calculations.
  /// \return True if every adjacent pair in \p Bndl is consecutive in memory.
  template <typename LoadOrStoreT, typename ValT>
  static bool areConsecutive(ArrayRef<ValT *> Bndl, ScalarEvolution &SE,
                             const DataLayout &DL) {
    static_assert(std::is_same<LoadOrStoreT, LoadInst>::value ||
                      std::is_same<LoadOrStoreT, StoreInst>::value,
                  "Expected Load or Store!");
    assert(isa<LoadOrStoreT>(Bndl[0]) && "Expected Load or Store!");
    auto *LastLS = cast<LoadOrStoreT>(Bndl[0]);
    for (Value *V : drop_begin(Bndl)) {
      assert(isa<LoadOrStoreT>(V) &&
             "Unimplemented: we only support StoreInst!");
      auto *LS = cast<LoadOrStoreT>(V);
      if (!VecUtils::areConsecutive(LastLS, LS, SE, DL))
        return false;
      LastLS = LS;
    }
    return true;
  }

  /// Return the number of vector lanes of \p Ty, or 1 if not a vector.
  ///
  /// Asserts that \p Ty is a fixed vector type or scalar.
  /// \param Ty Type to query.
  /// \return The number of elements if \p Ty is a fixed vector, otherwise 1.
  static unsigned getNumLanes(Type *Ty) {
    assert(!isa<ScalableVectorType>(Ty) && "Expect scalar or fixed vector");
    if (auto *FixedVecTy = dyn_cast<FixedVectorType>(Ty))
      return FixedVecTy->getNumElements();
    return 1u;
  }

  /// Return the expected vector lanes of \p V, or 1 if not a vector.
  ///
  /// Asserts that \p V has a fixed vector or scalar type.
  /// \param V Value whose expected type is queried.
  /// \return The number of lanes of \p V's expected type, or 1 if scalar.
  static unsigned getNumLanes(Value *V) {
    return VecUtils::getNumLanes(Utils::getExpectedType(V));
  }

  /// Return the total number of lanes across all values in \p Bndl.
  /// \param Bndl Bundle of values whose lanes are summed.
  /// \return The sum of expected vector lanes over all values in \p Bndl.
  static unsigned getNumLanes(ArrayRef<Value *> Bndl) {
    unsigned Lanes = 0;
    for (Value *V : Bndl)
      Lanes += getNumLanes(V);
    return Lanes;
  }

  /// Return the wide vector type `<NumElts x ElemTy>`.
  ///
  /// Works for both scalar and vector \p ElemTy.
  /// \param ElemTy Element type, or a vector type to widen further.
  /// \param NumElts Number of elements in the resulting vector.
  /// \return The widened fixed vector type.
  static Type *getWideType(Type *ElemTy, unsigned NumElts) {
    if (ElemTy->isVectorTy()) {
      auto *VecTy = cast<FixedVectorType>(ElemTy);
      ElemTy = VecTy->getElementType();
      NumElts = VecTy->getNumElements() * NumElts;
    }
    return FixedVectorType::get(ElemTy, NumElts);
  }
  /// Return the combined vector type for \p Bndl.
  ///
  /// Works even when the element types differ. For example: i8,i8,i16 will
  /// return `<4 x i8>`. Returns null if types are of mixed float/integer types.
  /// \param Bndl Bundle of instructions to combine.
  /// \param DL Data layout used for type sizes.
  /// \return The combined fixed vector type for \p Bndl, or null on mixed types.
  static Type *getCombinedVectorTypeFor(ArrayRef<Instruction *> Bndl,
                                        const DataLayout &DL) {
    assert(!Bndl.empty() && "Expected non-empty Bndl!");
    unsigned TotalBits = 0;
    unsigned MinElmBits = std::numeric_limits<unsigned>::max();
    Type *MinElmTy = nullptr;
    for (auto [Idx, V] : enumerate(Bndl)) {
      Type *ElmTy = getElementType(Utils::getExpectedType(V));

      unsigned ElmBits = Utils::getNumBits(ElmTy, DL);
      TotalBits += ElmBits * VecUtils::getNumLanes(V);
      if (ElmBits < MinElmBits) {
        MinElmBits = ElmBits;
        MinElmTy = ElmTy;
      }
    }
    unsigned NumElms = TotalBits / MinElmBits;
    return FixedVectorType::get(MinElmTy, NumElms);
  }
  /// Return the instruction in \p Instrs that is lowest in the BB.
  ///
  /// Expects that all instructions are in the same BB.
  /// \param Instrs Instructions to search.
  /// \return The instruction in \p Instrs that appears lowest in the BB.
  static Instruction *getLowest(ArrayRef<Instruction *> Instrs) {
    Instruction *LowestI = Instrs.front();
    for (auto *I : drop_begin(Instrs)) {
      if (LowestI->comesBefore(I))
        LowestI = I;
    }
    return LowestI;
  }
  /// Return the instruction in \p Instrs that is highest in the BB.
  ///
  /// Expects that all instructions are in the same BB.
  /// \param Instrs Instructions to search.
  /// \return The instruction in \p Instrs that appears highest in the BB.
  static Instruction *getHighest(ArrayRef<Instruction *> Instrs) {
    Instruction *HighestI = Instrs.front();
    for (auto *I : drop_begin(Instrs)) {
      if (I->comesBefore(HighestI))
        HighestI = I;
    }
    return HighestI;
  }
  /// Return the lowest instruction in \p Vals that is in \p BB.
  ///
  /// Returns nullptr if no instructions are found. Skips instructions not in
  /// \p BB.
  /// \param Vals Values to search; non-instructions are ignored.
  /// \param BB Basic block that candidate instructions must belong to.
  /// \return The lowest instruction in \p BB among \p Vals, or nullptr.
  static Instruction *getLowest(ArrayRef<Value *> Vals, BasicBlock *BB) {
    // Find the first Instruction in Vals that is also in `BB`.
    auto It = find_if(Vals, [BB](Value *V) {
      return isa<Instruction>(V) && cast<Instruction>(V)->getParent() == BB;
    });
    // If we couldn't find an instruction return nullptr.
    if (It == Vals.end())
      return nullptr;
    Instruction *FirstI = cast<Instruction>(*It);
    // Now look for the lowest instruction in Vals starting from one position
    // after FirstI.
    Instruction *LowestI = FirstI;
    for (auto *V : make_range(std::next(It), Vals.end())) {
      auto *I = dyn_cast<Instruction>(V);
      // Skip non-instructions.
      if (I == nullptr)
        continue;
      // Skips instructions not in \p BB.
      if (I->getParent() != BB)
        continue;
      // If `LowestI` comes before `I` then `I` is the new lowest.
      if (LowestI->comesBefore(I))
        LowestI = I;
    }
    return LowestI;
  }

  /// Return \p I, or the last PHI in the chain starting at \p I.
  ///
  /// If \p I is not a PHI it returns it. Else it walks down the instruction
  /// chain looking for the last PHI and returns it. Returns nullptr if \p I is
  /// nullptr.
  /// \param I Instruction to start from; may be nullptr.
  /// \return \p I, the last PHI in the chain, or nullptr if \p I is nullptr.
  static Instruction *getLastPHIOrSelf(Instruction *I) {
    Instruction *LastI = I;
    while (I != nullptr && isa<PHINode>(I)) {
      LastI = I;
      I = I->getNextNode();
    }
    return LastI;
  }

  /// Return the common scalar type of all values in \p Bndl, or nullptr.
  /// \param Bndl Bundle of values to inspect.
  /// \return The common scalar element type, or nullptr if types differ.
  static Type *tryGetCommonScalarType(ArrayRef<Value *> Bndl) {
    Value *V0 = Bndl[0];
    Type *Ty0 = Utils::getExpectedType(V0);
    Type *ScalarTy = VecUtils::getElementType(Ty0);
    for (auto *V : drop_begin(Bndl)) {
      Type *NTy = Utils::getExpectedType(V);
      Type *NScalarTy = VecUtils::getElementType(NTy);
      if (NScalarTy != ScalarTy)
        return nullptr;
    }
    return ScalarTy;
  }

  /// Return the common scalar type of all values in \p Bndl.
  ///
  /// Similar to tryGetCommonScalarType() but will assert that there is a common
  /// type. So this is faster in release builds as it won't iterate through the
  /// values.
  /// \param Bndl Bundle of values to inspect.
  /// \return The common scalar element type of all values in \p Bndl.
  static Type *getCommonScalarType(ArrayRef<Value *> Bndl) {
    Value *V0 = Bndl[0];
    Type *Ty0 = Utils::getExpectedType(V0);
    Type *ScalarTy = VecUtils::getElementType(Ty0);
    assert(tryGetCommonScalarType(Bndl) && "Expected common scalar type!");
    return ScalarTy;
  }
  /// Return the largest power of two that is less than or equal to \p Num.
  /// \param Num Value to round down to a power of two.
  /// \return The floor power of two of \p Num.
  LLVM_ABI static unsigned getFloorPowerOf2(unsigned Num);

  /// Form matching user bundles for each user of lane 0 in \p Bndl.
  ///
  /// Returns all complete user bundles found. \p Claimed contains instructions
  /// that have already been claimed by a bundle.
  /// \param Bndl Bundle whose lane-0 users seed candidate bundles.
  /// \param IMaps Maps between original and vectorized instructions.
  /// \param Claimed Set of instructions already claimed by another bundle.
  /// \return All complete user bundles found for lane-0 users of \p Bndl.
  LLVM_ABI static SmallVector<BundleTy>
  getNextUserBundles(ArrayRef<Value *> Bndl, const InstrMaps &IMaps,
                     SmallPtrSet<Instruction *, 4> &Claimed);

  /// Instructions and operands matched by `matchPack()`.
  struct PackPattern {
    /// InsertElement instructions that form the pack, in bottom-up order.
    ///
    /// The first instruction in `Instrs` is the bottom-most InsertElement
    /// instruction of the pack pattern.
    /// For example in this simple pack pattern:
    ///  %Pack0 = insertelement <2 x i8> poison, i8 %v0, i64 0
    ///  %Pack1 = insertelement <2 x i8> %Pack0, i8 %v1, i64 1
    /// this is [ %Pack1, %Pack0 ].
    SmallVector<Instruction *> Instrs;
    /// External operands packed into the vector, in bottom-up order.
    ///
    /// These are the values that get packed into a vector, skipping the ones in
    /// `Instrs`. The operands start from the operands of the bottom-most
    /// insert. So in our example this would be [ %v1, %v0 ].
    SmallVector<Value *> Operands;
  };

  /// Match a pack pattern ending at InsertElement \p I, if any.
  ///
  /// If \p I is the last instruction of a pack pattern (i.e., an InsertElement
  /// into a vector), then this function returns the instructions in the pack
  /// and the operands in the pack, else returns nullopt.
  /// Here is an example of a matched pattern:
  ///  %PackA0 = insertelement <2 x i8> poison, i8 %v0, i64 0
  ///  %PackA1 = insertelement <2 x i8> %PackA0, i8 %v1, i64 1
  /// TODO: this currently detects only simple canonicalized patterns.
  /// \param I Candidate bottom-most InsertElement of a pack pattern.
  /// \return The matched pack pattern, or nullopt if \p I is not a pack.
  static std::optional<PackPattern> matchPack(Instruction *I) {
    // TODO: Support vector pack patterns.
    // TODO: Support out-of-order inserts.

    // Early return if `I` is not an Insert.
    if (!isa<InsertElementInst>(I))
      return std::nullopt;
    auto *BB0 = I->getParent();
    // The pack contains as many instrs as the lanes of the bottom-most Insert
    unsigned ExpectedNumInserts = VecUtils::getNumLanes(I);
    assert(ExpectedNumInserts >= 2 && "Expected at least 2 inserts!");
    PackPattern Pack;
    Pack.Operands.resize(ExpectedNumInserts);
    // Collect the inserts by walking up the use-def chain.
    Instruction *InsertI = I;
    for (auto ExpectedLane : reverse(seq<unsigned>(ExpectedNumInserts))) {
      if (InsertI == nullptr)
        return std::nullopt;
      if (InsertI->getParent() != BB0)
        return std::nullopt;
      // Check the lane.
      auto *LaneC = dyn_cast<ConstantInt>(InsertI->getOperand(2));
      if (LaneC == nullptr || LaneC->getSExtValue() != ExpectedLane)
        return std::nullopt;
      Pack.Instrs.push_back(InsertI);
      Pack.Operands[ExpectedLane] = InsertI->getOperand(1);

      Value *Op = InsertI->getOperand(0);
      if (ExpectedLane == 0) {
        // Check the topmost insert. The operand should be a Poison.
        if (!isa<PoisonValue>(Op))
          return std::nullopt;
      } else {
        InsertI = dyn_cast<InsertElementInst>(Op);
      }
    }
    return Pack;
  }

  /// Extract the element of type \p ExtrTy at \p Lane from \p FromVec.
  ///
  /// Emits the necessary instruction sequence before \p WhereIt and returns
  /// the extracted value. This handles both vectors and scalars. In the vector
  /// case it extracts an N-wide element (with N dictated by \p ExtrTy).
  /// \param FromVec Vector value to unpack from.
  /// \param ExtrTy Type of the extracted element (scalar or vector).
  /// \param Lane Starting lane to extract.
  /// \param WhereIt Insertion point for the generated instructions.
  /// \return The extracted scalar or sub-vector value.
  static Value *unpack(Value *FromVec, Type *ExtrTy, unsigned Lane,
                       BasicBlock::iterator WhereIt) {
    assert(isa<FixedVectorType>(FromVec->getType()) && "Expected vector!");
    auto &Ctx = FromVec->getContext();
    if (!ExtrTy->isVectorTy()) {
      // For scalar elements we emit a single ExtractElementInst.
      assert(Lane <
                 cast<FixedVectorType>(FromVec->getType())->getNumElements() &&
             "Out of bounds!");
      assert(ExtrTy ==
                 cast<FixedVectorType>(FromVec->getType())->getElementType() &&
             "Expected same element type!");
      Constant *ExtractLaneC =
          ConstantInt::getSigned(Type::getInt32Ty(Ctx), Lane);
      // Note: This may be folded into a Constant if FromVec is a Constant.
      return ExtractElementInst::create(FromVec, ExtractLaneC, WhereIt, Ctx,
                                        "Unpack");
    }
    // For vector elements we emit a shuffle.
    // For example, extracting lanes 2 and 3 of a <4 x i32> vector %vec:
    //  shufflevector <4 x i32> %vec, <4 x i32> poison, <2 x i32> <i32 2, i32 3>
    auto *VecTy = cast<FixedVectorType>(FromVec->getType());
    auto *ExtrVecTy = cast<FixedVectorType>(ExtrTy);
    assert(ExtrVecTy->getElementType() == VecTy->getElementType() &&
           "Expected same element type!");
    SmallVector<int, 4> Mask;
    for (unsigned Idx = 0, E = ExtrVecTy->getNumElements(); Idx != E; ++Idx) {
      int MaskLane = Lane + Idx;
      assert((unsigned)MaskLane <
                 cast<FixedVectorType>(FromVec->getType())->getNumElements() &&
             "Out of bounds!");
      Mask.push_back(MaskLane);
    }
    return ShuffleVectorInst::create(FromVec, PoisonValue::get(VecTy), Mask,
                                     WhereIt, Ctx, "Unpack");
  }

  /// Input iterator over (lane, value) pairs in a value range.
  ///
  /// For example, given a range: {i32 %v0, <2 x i32> %v1, i32 %v2} we get:
  ///  Lane Elm
  ///   0   %v0
  ///   1   %v1
  ///   3   %v2
  template <typename RangeIteratorT> class LaneValueEnumerator {
    /// Points to current element.
    RangeIteratorT It;
    RangeIteratorT ItE;
    /// Accumulator of lanes.
    unsigned Lane;

  public:
    /// Construct an enumerator over [\p Begin, \p End) starting at \p BeginLane.
    ///
    /// \p BeginLane may be non-zero, but the caller must ensure it matches the
    /// lane corresponding to \p Begin.
    /// \param Begin Start of the value range.
    /// \param End End of the value range.
    /// \param BeginLane Lane index corresponding to \p Begin.
    LaneValueEnumerator(RangeIteratorT Begin, RangeIteratorT End,
                        unsigned BeginLane)
        : It(Begin), ItE(End), Lane(BeginLane) {}
    /// Iterator category tag for this input iterator.
    using iterator_catecotry = std::input_iterator_tag;
    /// Value type yielded by this enumerator.
    ///
    /// NOTE: dereference returns by value instead of by reference.
    using value_type = std::pair<unsigned, Value *>;
    /// Distance type for this iterator.
    using difference_type = std::ptrdiff_t;
    /// Pointer type for this iterator.
    using pointer = std::pair<unsigned, Value *> *;
    /// Reference type for this iterator.
    using reference = std::pair<unsigned, Value *> &;
    /// Advance to the next value and update the lane accumulator.
    /// \return A copy of this enumerator after advancement.
    LaneValueEnumerator operator++() {
      assert(It != ItE && "Already at end!");
      auto *Ty = Utils::getExpectedType(*It);
      if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
        Lane += VecTy->getNumElements();
      } else {
        assert(!isa<VectorType>(Ty) && "Expected scalar type!");
        Lane += 1;
      }
      ++It;
      return *this;
    }
    /// Return the current (lane, value) pair.
    /// \return The current (lane, value) pair.
    value_type operator*() const { return {Lane, *It}; }
    /// Return true if this enumerator equals \p Other.
    /// \param Other Enumerator to compare against.
    /// \return True if both enumerators point to the same position.
    bool operator==(const LaneValueEnumerator &Other) const {
      return It == Other.It;
    }
    /// Return true if this enumerator differs from \p Other.
    /// \param Other Enumerator to compare against.
    /// \return True if the enumerators point to different positions.
    bool operator!=(const LaneValueEnumerator &Other) const {
      return !(*this == Other);
    }
  };

  /// Create a LaneValueEnumerator range over \p Range.
  ///
  /// Can be used in for loops like: `for (auto [Lane, V] : enumerateLanes(Range))`
  /// \param Range Container of values to enumerate by lane.
  /// \return An iterator range of (lane, value) pairs over \p Range.
  template <typename ValueContainerT>
  static auto enumerateLanes(const ValueContainerT &Range) {
    auto Begin = LaneValueEnumerator<decltype(Range.begin())>(Range.begin(),
                                                              Range.end(), 0);
    auto End = LaneValueEnumerator<decltype(Range.begin())>(Range.end(),
                                                            Range.end(), 0);
    return make_range(Begin, End);
  }

#ifndef NDEBUG
  /// Dump \p Bndl to the debug stream.
  /// \param Bndl Bundle of values to dump.
  LLVM_DUMP_METHOD static void dump(ArrayRef<Value *> Bndl);
  /// Dump \p Bndl to the debug stream.
  /// \param Bndl Bundle of instructions to dump.
  LLVM_DUMP_METHOD static void dump(ArrayRef<Instruction *> Bndl);
#endif // NDEBUG
};

} // namespace sandboxir

} // namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_VECUTILS_H
