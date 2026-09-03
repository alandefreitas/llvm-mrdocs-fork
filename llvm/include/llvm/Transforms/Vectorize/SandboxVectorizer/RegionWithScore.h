//===- RegionWithScore.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A region with score tracking for added/removed instructions.
//

#ifndef LLVM_SANDBOXIR_REGIONWITHSCORE_H
#define LLVM_SANDBOXIR_REGIONWITHSCORE_H

#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/SandboxIR/Region.h"

namespace llvm::sandboxir {

/// Vectorization Score (cost) tracking class.
class ScoreBoard {
  const Region &Rgn;
  const TargetTransformInfo &TTI;
  constexpr static TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  /// The cost of all instructions added to the region.
  InstructionCost AfterCost = 0;
  /// The cost of all instructions that got removed and replaced by new ones.
  InstructionCost BeforeCost = 0;
  /// Helper for both add() and remove(). \Returns the TTI cost of \p I.
  LLVM_ABI InstructionCost getCost(Instruction *I) const;
  /// No need to allow copies.
  ScoreBoard(const ScoreBoard &) = delete;
  const ScoreBoard &operator=(const ScoreBoard &) = delete;

public:
  /// Construct a scoreboard for \p Rgn using \p TTI for costs.
  ///
  /// \param Rgn Region whose instruction costs are tracked.
  /// \param TTI Target transform info used to compute instruction costs.
  ScoreBoard(Region &Rgn, const TargetTransformInfo &TTI)
      : Rgn(Rgn), TTI(TTI) {}
  /// Mark \p I as a newly added instruction to the region.
  ///
  /// \param I Instruction whose cost is added to the after-cost.
  void add(Instruction *I) { AfterCost += getCost(I); }
  /// Mark \p I as a deleted instruction from the region.
  ///
  /// \param I Instruction whose cost is added to the before-cost.
  LLVM_ABI void remove(Instruction *I);
  /// Return the cost of the newly added instructions.
  /// @return The cost of the newly added instructions.
  InstructionCost getAfterCost() const { return AfterCost; }
  /// Return the cost of the removed instructions.
  /// @return The cost of the removed instructions.
  InstructionCost getBeforeCost() const { return BeforeCost; }

#ifndef NDEBUG
  /// Print the before and after costs to \p OS.
  ///
  /// \param OS Destination stream.
  void dump(raw_ostream &OS) const {
    OS << "BeforeCost: " << BeforeCost << "\n";
    OS << "AfterCost:  " << AfterCost << "\n";
  }
  /// Dump the before and after costs to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif // NDEBUG
};

/// A Region class that tracks its instructions score.
class RegionWithScore final : public Region {
  /// Keeps track of cost of instructions added and removed.
  ScoreBoard Scoreboard;

  void add(Instruction *I) override {
    addRaw(I);
    // Keep track of the instruction cost.
    Scoreboard.add(I);
  }
  friend class RegionsFromBBs; // For add().

  void remove(Instruction *I) override {
    // Keep track of the instruction cost. This need to be done *before* we
    // remove `I` from the region.
    Scoreboard.remove(I);
    Region::remove(I);
  }

public:
  /// Construct an empty scored region in \p Ctx using \p TTI for costs.
  ///
  /// \param Ctx SandboxIR context that owns this region.
  /// \param TTI Target transform info used to compute instruction costs.
  RegionWithScore(Context &Ctx, const TargetTransformInfo &TTI)
      : Region(Ctx, RegionClassID::RegionWithScoreID), Scoreboard(*this, TTI) {}
  /// Construct a scored region by taking ownership of \p Rgn.
  ///
  /// \param Rgn Existing region to move into this scored region.
  /// \param TTI Target transform info used to compute instruction costs.
  RegionWithScore(Region &&Rgn, const TargetTransformInfo &TTI)
      : Region(std::move(Rgn)), Scoreboard(*this, TTI) {}
  /// Return true if \p From is a RegionWithScore.
  ///
  /// Used by isa, cast, and similar type-inquiry helpers.
  /// \param From Region to test.
  /// @return True if \p From is a RegionWithScore.
  static bool classof(const Region *From) {
    return From->getSubclassID() == RegionClassID::RegionWithScoreID;
  }

  /// Return the scoreboard that tracks instruction costs.
  /// @return The scoreboard that tracks instruction costs.
  const ScoreBoard &getScoreboard() const { return Scoreboard; }

  /// Create scored regions from sandboxvec metadata in \p F.
  ///
  /// \param F Function whose instructions may carry region metadata.
  /// \param TTI Target transform info used to compute instruction costs.
  /// @return The scored regions created from metadata in \p F.
  LLVM_ABI static SmallVector<std::unique_ptr<RegionWithScore>>
  createRegionsFromMD(Function &F, const TargetTransformInfo &TTI);
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_REGIONWITHSCORE_H
