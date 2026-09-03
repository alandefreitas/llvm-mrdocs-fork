//===- InstrMaps.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVEC_PASSES_INSTRMAPS_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVEC_PASSES_INSTRMAPS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/SandboxIR/Context.h"
#include "llvm/SandboxIR/Instruction.h"
#include "llvm/SandboxIR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Vectorize/SandboxVectorizer/VecUtils.h"

namespace llvm::sandboxir {

class LegalityResult;

/// Describes how to vectorize a bundle of values.
struct Action {
  /// Index of this action in the owning actions vector.
  unsigned Idx = 0;
  /// Legality analysis result that decided how to vectorize the bundle.
  const LegalityResult *LegalityRes = nullptr;
  /// Bundle of original values to vectorize.
  SmallVector<Value *, 4> Bndl;
  /// User bundle that this recursive vectorization step came from.
  SmallVector<Value *> UserBndl;
  /// Recursion depth of this action in the vectorization walk.
  unsigned Depth;
  /// Actions for the already-vectorized operands of this bundle.
  SmallVector<Action *> Operands;
  /// Vector value produced when this action is emitted, if any.
  Value *Vec = nullptr;
  /// Construct an action for \p B at \p Depth with legality result \p LR.
  ///
  /// \param LR Legality result describing how to vectorize the bundle.
  /// \param B Bundle of original values to vectorize.
  /// \param UB User bundle that this recursive step originates from.
  /// \param Depth Recursion depth of this action in the vectorization walk.
  Action(const LegalityResult *LR, ArrayRef<Value *> B, ArrayRef<Value *> UB,
         unsigned Depth)
      : LegalityRes(LR), Bndl(B), UserBndl(UB), Depth(Depth) {}
#ifndef NDEBUG
  /// Print a textual representation of this action to \p OS.
  ///
  /// \param OS Destination stream.
  void print(raw_ostream &OS) const;
  /// Dump this action to the debug stream.
  void dump() const;
  /// Write a textual representation of \p A to \p OS.
  ///
  /// \param OS Destination stream.
  /// \param A Action to print.
  /// \return The output stream \p OS.
  friend raw_ostream &operator<<(raw_ostream &OS, const Action &A) {
    A.print(OS);
    return OS;
  }
#endif // NDEBUG
};

/// Maps the original instructions to the vectorized instrs and the reverse.
/// For now an original instr can only map to a single vector.
class InstrMaps {
  /// A map from the original values that got combined into vectors, to the
  /// vectorization Action.
  DenseMap<Value *, Action *> OrigToVectorMap;
  /// A map from the vec Action to a map of the original value to its lane.
  /// Please note that for constant vectors, there may multiple original values
  /// with the same lane, as they may be coming from vectorizing different
  /// original values.
  DenseMap<Action *, DenseMap<Value *, unsigned>> VectorToOrigLaneMap;
  std::optional<Context::CallbackID> EraseInstrCB;

public:
  /// Construct an empty instruction map.
  InstrMaps() = default;
  /// Destroy this instruction map.
  ~InstrMaps() = default;
  /// Return true if \p Orig was vectorized.
  ///
  /// \param Orig Original value to query.
  /// \return True if \p Orig was vectorized.
  bool isVectorized(Value *Orig) const {
    return OrigToVectorMap.contains(Orig);
  }
  /// Return the vectorization action for \p Orig, or nullptr if not found.
  ///
  /// \param Orig Original value that may have been vectorized.
  /// \return The vectorization action for \p Orig, or nullptr if not found.
  Action *getVectorForOrig(Value *Orig) const {
    auto It = OrigToVectorMap.find(Orig);
    return It != OrigToVectorMap.end() ? It->second : nullptr;
  }
  /// Return the lane of \p Orig within \p Vec, or nullopt if not found.
  ///
  /// \param Vec Vectorization action that combined original values.
  /// \param Orig Original value whose lane is queried.
  /// \return The lane of \p Orig within \p Vec, or nullopt if not found.
  std::optional<unsigned> getOrigLane(Action *Vec, Value *Orig) const {
    auto It1 = VectorToOrigLaneMap.find(Vec);
    if (It1 == VectorToOrigLaneMap.end())
      return std::nullopt;
    const auto &OrigToLaneMap = It1->second;
    auto It2 = OrigToLaneMap.find(Orig);
    if (It2 == OrigToLaneMap.end())
      return std::nullopt;
    return It2->second;
  }
  /// Update the map to reflect that \p Origs got vectorized into \p Vec.
  ///
  /// \param Origs Original values combined into the vector action.
  /// \param Vec Vectorization action produced for \p Origs.
  void registerVector(ArrayRef<Value *> Origs, Action *Vec) {
    auto &OrigToLaneMap = VectorToOrigLaneMap[Vec];
    unsigned Lane = 0;
    for (Value *Orig : Origs) {
      auto Pair = OrigToVectorMap.try_emplace(Orig, Vec);
      assert(Pair.second && "Orig already exists in the map!");
      (void)Pair;
      OrigToLaneMap[Orig] = Lane;
      Lane += VecUtils::getNumLanes(Orig);
    }
  }
  /// Clear all original-to-vector and vector-to-lane mappings.
  void clear() {
    OrigToVectorMap.clear();
    VectorToOrigLaneMap.clear();
  }
#ifndef NDEBUG
  /// Print the original-to-vector map to \p OS.
  ///
  /// \param OS Destination stream.
  void print(raw_ostream &OS) const {
    OS << "OrigToVectorMap:\n";
    for (auto [Orig, Vec] : OrigToVectorMap)
      OS << *Orig << " : " << *Vec << "\n";
  }
  /// Dump the instruction maps to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif
};
} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVEC_PASSES_INSTRMAPS_H
