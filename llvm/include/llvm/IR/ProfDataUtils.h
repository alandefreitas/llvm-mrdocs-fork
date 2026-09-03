//===- llvm/IR/ProfDataUtils.h - Profiling Metadata Utilities ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// This file contains the declarations for profiling metadata utility
/// functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PROFDATAUTILS_H
#define LLVM_IR_PROFDATAUTILS_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include <cstddef>
#include <type_traits>

namespace llvm {
/// Names of profiling metadata labels stored in \c !prof nodes.
struct MDProfLabels {
  /// Label for branch-weight metadata stored in \c !prof nodes.
  LLVM_ABI static const char *BranchWeights;
  /// Label for value-profile metadata stored in \c !prof nodes.
  LLVM_ABI static const char *ValueProfile;
  /// Label for function entry-count metadata stored in \c !prof nodes.
  LLVM_ABI static const char *FunctionEntryCount;
  /// Label for synthetic function entry-count metadata stored in \c !prof nodes.
  LLVM_ABI static const char *SyntheticFunctionEntryCount;
  /// Label for branch-weight provenance metadata stored in \c !prof nodes.
  LLVM_ABI static const char *ExpectedBranchWeights;
  /// Label for unknown branch-weight metadata stored in \c !prof nodes.
  LLVM_ABI static const char *UnknownBranchWeightsMarker;
};

/// Command-line option that disables profile-metadata propagation fixes.
extern LLVM_ABI cl::opt<bool> ProfcheckDisableMetadataFixes;

/// Profile-based loop metadata that should be accessed only by using
/// \c llvm::getLoopEstimatedTripCount and \c llvm::setLoopEstimatedTripCount.
LLVM_ABI extern const char *LLVMLoopEstimatedTripCount;

/// Checks if an Instruction has MD_prof Metadata
///
/// \param I The instruction to check
/// \returns True if I has MD_prof metadata. False otherwise.
LLVM_ABI bool hasProfMD(const Instruction &I);

/// Checks if an MDNode contains Branch Weight Metadata
///
/// \param ProfileData The MD_prof metadata node to inspect
/// \returns True if ProfileData contains branch weight metadata. False
/// otherwise.
LLVM_ABI bool isBranchWeightMD(const MDNode *ProfileData);

/// Checks if an MDNode contains value profiling Metadata
///
/// \param ProfileData The MD_prof metadata node to inspect
/// \returns True if ProfileData contains value profiling metadata. False
/// otherwise.
LLVM_ABI bool isValueProfileMD(const MDNode *ProfileData);

/// Checks if an instructions has Branch Weight Metadata
///
/// \param I The instruction to check
/// \returns True if I has an MD_prof node containing Branch Weights. False
/// otherwise.
LLVM_ABI bool hasBranchWeightMD(const Instruction &I);

/// Checks if an instructions has valid Branch Weight Metadata
///
/// \param I The instruction to check
/// \returns True if I has an MD_prof node containing valid Branch Weights,
/// i.e., one weight for each successor. False otherwise.
LLVM_ABI bool hasValidBranchWeightMD(const Instruction &I);

/// Get the branch weights metadata node
///
/// \param I The Instruction to get the weights from.
/// \returns A pointer to I's branch weights metadata node, if it exists.
/// Nullptr otherwise.
LLVM_ABI MDNode *getBranchWeightMDNode(const Instruction &I);

/// Get the valid branch weights metadata node
///
/// \param I The Instruction to get the weights from.
/// \returns A pointer to I's valid branch weights metadata node, if it exists.
/// Nullptr otherwise.
LLVM_ABI MDNode *getValidBranchWeightMDNode(const Instruction &I);

/// Check if Branch Weight Metadata has an "expected" field from an llvm.expect*
/// intrinsic
///
/// \param I The instruction to check
/// \returns True if I has branch-weight metadata with an "expected" field from
/// an llvm.expect* intrinsic. False otherwise.
LLVM_ABI bool hasBranchWeightOrigin(const Instruction &I);

/// Check if Branch Weight Metadata has an "expected" field from an llvm.expect*
/// intrinsic
///
/// \param ProfileData The MD_prof metadata node to inspect
/// \returns True if ProfileData has an "expected" field from an llvm.expect*
/// intrinsic. False otherwise.
LLVM_ABI bool hasBranchWeightOrigin(const MDNode *ProfileData);

/// Return the offset to the first branch weight data
///
/// \param ProfileData The MD_prof metadata node to inspect
/// \returns The index of the first branch weight operand in ProfileData.
LLVM_ABI unsigned getBranchWeightOffset(const MDNode *ProfileData);

/// Return the number of branch weights stored in \p ProfileData
///
/// \param ProfileData The MD_prof metadata node to inspect
/// \returns The number of branch weights in ProfileData.
LLVM_ABI unsigned getNumBranchWeights(const MDNode &ProfileData);

/// Extract branch weights from MD_prof metadata
///
/// \param ProfileData A pointer to an MDNode.
/// \param [out] Weights An output vector to fill with branch weights
/// \returns True if weights were extracted, False otherwise. When false Weights
/// will be cleared.
LLVM_ABI bool extractBranchWeights(const MDNode *ProfileData,
                                   SmallVectorImpl<uint32_t> &Weights);

/// Faster version of extractBranchWeights() that skips checks and must only
/// be called with "branch_weights" metadata nodes. Supports uint32_t.
///
/// \param ProfileData A pointer to a "branch_weights" MDNode.
/// \param [out] Weights An output vector to fill with branch weights
LLVM_ABI void extractFromBranchWeightMD32(const MDNode *ProfileData,
                                          SmallVectorImpl<uint32_t> &Weights);

/// Faster version of extractBranchWeights() that skips checks and must only
/// be called with "branch_weights" metadata nodes. Supports uint64_t.
///
/// \param ProfileData A pointer to a "branch_weights" MDNode.
/// \param [out] Weights An output vector to fill with branch weights
LLVM_ABI void extractFromBranchWeightMD64(const MDNode *ProfileData,
                                          SmallVectorImpl<uint64_t> &Weights);

/// Extract branch weights attatched to an Instruction
///
/// \param I The Instruction to extract weights from.
/// \param [out] Weights An output vector to fill with branch weights
/// \returns True if weights were extracted, False otherwise. When false Weights
/// will be cleared.
LLVM_ABI bool extractBranchWeights(const Instruction &I,
                                   SmallVectorImpl<uint32_t> &Weights);

/// Extract branch weights from a conditional branch or select Instruction.
///
/// \param I The instruction to extract branch weights from.
/// \param [out] TrueVal will contain the branch weight for the True branch
/// \param [out] FalseVal will contain the branch weight for the False branch
/// \returns True on success with profile weights filled in. False if no
/// metadata or invalid metadata was found.
LLVM_ABI bool extractBranchWeights(const Instruction &I, uint64_t &TrueVal,
                                   uint64_t &FalseVal);

/// Retrieve the total of all weights from MD_prof data.
///
/// \param ProfileData The profile data to extract the total weight from
/// \param [out] TotalWeights input variable to fill with total weights
/// \returns True on success with profile total weights filled in. False if no
/// metadata was found.
LLVM_ABI bool extractProfTotalWeight(const MDNode *ProfileData,
                                     uint64_t &TotalWeights);

/// Retrieve the total of all weights from an instruction.
///
/// \param I The instruction to extract the total weight from
/// \param [out] TotalWeights input variable to fill with total weights
/// \returns True on success with profile total weights filled in. False if no
/// metadata was found.
LLVM_ABI bool extractProfTotalWeight(const Instruction &I,
                                     uint64_t &TotalWeights);

/// Create a new `branch_weights` metadata node and add or overwrite
/// a `prof` metadata reference to instruction `I`.
/// \param I the Instruction to set branch weights on.
/// \param Weights an array of weights to set on instruction I.
/// \param IsExpected were these weights added from an llvm.expect* intrinsic.
/// \param ElideAllZero if true, drop profile metadata when all weights are
/// zero.
LLVM_ABI void setBranchWeights(Instruction &I, ArrayRef<uint32_t> Weights,
                               bool IsExpected, bool ElideAllZero = false);

/// Push the weights right to fit in uint32_t.
///
/// \param Weights The 64-bit weights to scale down to 32 bits.
/// \returns The weights scaled down to fit in uint32_t.
LLVM_ABI SmallVector<uint32_t> fitWeights(ArrayRef<uint64_t> Weights);

/// Variant of `setBranchWeights` where the `Weights` will be fit first to
/// uint32_t by shifting right.
///
/// \param I the Instruction to set branch weights on.
/// \param Weights an array of 64-bit weights to fit and set on instruction I.
/// \param IsExpected were these weights added from an llvm.expect* intrinsic.
/// \param ElideAllZero if true, drop profile metadata when all weights are
/// zero.
LLVM_ABI void setFittedBranchWeights(Instruction &I, ArrayRef<uint64_t> Weights,
                                     bool IsExpected,
                                     bool ElideAllZero = false);

/// downscale the given weights preserving the ratio. If the maximum value is
/// not already known and not provided via \param KnownMaxCount , it will be
/// obtained from \param Weights.
///
/// \param Weights The 64-bit weights to downscale.
/// \returns The downscaled 32-bit weights, preserving the original ratio.
LLVM_ABI SmallVector<uint32_t>
downscaleWeights(ArrayRef<uint64_t> Weights,
                 std::optional<uint64_t> KnownMaxCount = std::nullopt);

/// Calculate what to divide by to scale counts.
///
/// Given the maximum count, calculate a divisor that will scale all the
/// weights to strictly less than std::numeric_limits<uint32_t>::max().
///
/// \param MaxCount The largest count that will be scaled.
/// \returns The divisor to use when scaling counts down to 32 bits.
inline uint64_t calculateCountScale(uint64_t MaxCount) {
  return MaxCount < std::numeric_limits<uint32_t>::max()
             ? 1
             : MaxCount / std::numeric_limits<uint32_t>::max() + 1;
}

/// Scale an individual branch count.
///
/// Scale a 64-bit weight down to 32-bits using \c Scale.
///
/// \param Count The 64-bit branch count to scale.
/// \param Scale The divisor returned by \c calculateCountScale.
/// \returns The 32-bit scaled branch count.
inline uint32_t scaleBranchCount(uint64_t Count, uint64_t Scale) {
  uint64_t Scaled = Count / Scale;
  assert(Scaled <= std::numeric_limits<uint32_t>::max() && "overflow 32-bits");
  return Scaled;
}

/// Specify that this terminator's branch weights are unknown at compile time.
///
/// This should only be called by passes, and never as a default behavior in
/// e.g. MDBuilder. The goal is to use this info to validate passes do not
/// accidentally drop profile info, and this API is called in cases where the
/// pass explicitly cannot provide that info. Defaulting it in would hide bugs
/// where the pass forgets to transfer over or otherwise specify profile info.
/// Use `PassName` to capture the pass name (i.e. DEBUG_TYPE) for
/// debuggability.
///
/// \param I The terminator instruction to mark.
/// \param PassName The pass name (typically DEBUG_TYPE) recorded for
/// debugging.
LLVM_ABI void setExplicitlyUnknownBranchWeights(Instruction &I,
                                                StringRef PassName);

/// Set unknown branch weights only if the parent function has an entry count.
///
/// Like setExplicitlyUnknownBranchWeights(...), but only sets unknown branch
/// weights in the new instruction if the parent function of the original
/// instruction has an entry count. This is to not confuse users by injecting
/// profile data into non-profiled functions. If \p F is nullptr, we will fetch
/// the function from \p I.
///
/// \param I The instruction to mark.
/// \param PassName The pass name (typically DEBUG_TYPE) recorded for
/// debugging.
/// \param F Optional parent function; if null, taken from \p I.
LLVM_ABI void
setExplicitlyUnknownBranchWeightsIfProfiled(Instruction &I, StringRef PassName,
                                            const Function *F = nullptr);

/// Returns a metadata node containing unknown branch weights if the function
/// has an entry count, otherwise returns nullptr.
///
/// \param F The function whose entry count is checked.
/// \param PassName The pass name (typically DEBUG_TYPE) recorded for
/// debugging.
/// \returns A metadata node with unknown branch weights if F has an entry
/// count; nullptr otherwise.
LLVM_ABI MDNode *
getExplicitlyUnknownBranchWeightsIfProfiled(Function &F, StringRef PassName);

/// Analogous to setExplicitlyUnknownBranchWeights, but for functions and their
/// entry counts.
///
/// \param F The function whose entry count is marked unknown.
/// \param PassName The pass name (typically DEBUG_TYPE) recorded for
/// debugging.
LLVM_ABI void setExplicitlyUnknownFunctionEntryCount(Function &F,
                                                     StringRef PassName);

/// Check whether \p MD is explicitly-unknown profile metadata.
///
/// \param MD The MD_prof metadata node to inspect
/// \returns True if MD is explicitly-unknown profile metadata. False
/// otherwise.
LLVM_ABI bool isExplicitlyUnknownProfileMetadata(const MDNode &MD);

/// Check whether \p I has explicitly-unknown branch-weight metadata.
///
/// \param I The instruction to check
/// \returns True if I has explicitly-unknown branch-weight metadata. False
/// otherwise.
LLVM_ABI bool hasExplicitlyUnknownBranchWeights(const Instruction &I);

/// Scaling the profile data attached to 'I' using the ratio of S/T.
///
/// \param I The instruction whose profile data is scaled.
/// \param S Numerator of the scale ratio.
/// \param T Denominator of the scale ratio.
LLVM_ABI void scaleProfData(Instruction &I, uint64_t S, uint64_t T);

/// Apply a metadata callback if \p V is an instruction and profiling is enabled.
///
/// If profiling is disabled (\c ProfcheckDisableMetadataFixes is true) or
/// \p V is not an Instruction, the callback will not be invoked.
///
/// \param V The value to inspect; the callback runs only if this is an
/// Instruction.
/// \param setMetadataCallback Called with \p V cast to Instruction when
/// profiling is enabled.
LLVM_ABI void applyProfMetadataIfEnabled(
    Value *V, llvm::function_ref<void(Instruction *)> setMetadataCallback);

/// Get the branch weights of a branch conditioned on b1 || b2.
///
/// b1 and b2 are 2 booleans that are the conditions of 2 branches for which we
/// have the branch weights B1 and B2, respectively. In both B1 and B2, the
/// first position (index 0) is for the 'true' branch, and the second position
/// (index 1) is for the 'false' branch.
///
/// \param B1 Branch weights of the first condition, [true, false].
/// \param B2 Branch weights of the second condition, [true, false].
/// \returns Branch weights for the disjunction, [true, false].
template <typename T1, typename T2,
          typename = typename std::enable_if<
              std::is_arithmetic_v<T1> && std::is_arithmetic_v<T2> &&
              sizeof(T1) <= sizeof(uint64_t) && sizeof(T2) <= sizeof(uint64_t)>>
inline SmallVector<uint64_t, 2>
getDisjunctionWeights(const SmallVector<T1, 2> &B1,
                      const SmallVector<T2, 2> &B2) {
  // For the first conditional branch, the probability the "true" case is taken
  // is p(b1) = B1[0] / (B1[0] + B1[1]). The "false" case's probability is
  // p(not b1) = B1[1] / (B1[0] + B1[1]).
  // Similarly for the second conditional branch and B2.
  //
  // The probability of the new branch NOT being taken is:
  // not P = p((not b1) and (not b2)) =
  //       = B1[1] / (B1[0]+B1[1]) * B2[1] / (B2[0]+B2[1]) =
  //       = B1[1] * B2[1] / (B1[0] + B1[1]) * (B2[0] + B2[1])
  // Then the probability of it being taken is: P = 1 - (not P).
  // The denominator will be the same as above, and the numerator of P will be:
  // (B1[0] + B1[1]) * (B2[0] + B2[1]) - B1[1]*B2[1]
  // Which then reduces to what's shown below (out of the 4 terms coming out of
  // the product of sums, the subtracted one cancels out).
  assert(B1.size() == 2);
  assert(B2.size() == 2);

  uint64_t FalseWeight, TrueWeight;

  if (!ProfcheckDisableMetadataFixes) {
    FalseWeight = static_cast<uint64_t>(B1[1]) * B2[1];
    TrueWeight =
        static_cast<uint64_t>(B1[0]) * (static_cast<uint64_t>(B2[0]) + B2[1]) +
        static_cast<uint64_t>(B1[1]) * B2[0];
  } else {
    FalseWeight = B1[1] * B2[1];
    TrueWeight = B1[0] * (B2[0] + B2[1]) + B1[1] * B2[0];
  }
  return {TrueWeight, FalseWeight};
}
} // namespace llvm
#endif
