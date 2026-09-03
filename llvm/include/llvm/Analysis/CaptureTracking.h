//===----- llvm/Analysis/CaptureTracking.h - Pointer capture ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains routines that help determine which pointers are captured.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CAPTURETRACKING_H
#define LLVM_ANALYSIS_CAPTURETRACKING_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ModRef.h"

namespace llvm {

  class Value;
  class Use;
  class CaptureInfo;
  class DataLayout;
  class Instruction;
  class DominatorTree;
  class LoopInfo;
  class Function;
  template <typename Fn> class function_ref;

  /// Return the default max uses to explore for capture tracking.
  ///
  /// Used by the PointerMayBeCaptured family of analyses before giving up.
  /// @return Default maximum number of uses to explore.
  LLVM_ABI unsigned getDefaultMaxUsesToExploreForCaptureTracking();

  /// Return true if this pointer may be captured by the enclosing function.
  ///
  /// The enclosing function is required to exist. This routine can be
  /// expensive, so consider caching the results.
  ///
  /// This function only considers captures of the passed value via its def-use
  /// chain, without considering captures of values it may be based on, or
  /// implicit captures such as for external globals.
  /// @param V Pointer value whose captures are queried.
  /// @param ReturnCaptures Whether returning the value (or part of it) counts
  /// as capturing it.
  /// @param MaxUsesToExplore How many uses to explore before giving up; zero
  /// means use the default.
  /// @return True if the pointer may be captured by the enclosing function.
  LLVM_ABI bool PointerMayBeCaptured(const Value *V, bool ReturnCaptures,
                                     unsigned MaxUsesToExplore = 0);

  /// Result of a PointerMayBeCaptured query, which includes the captured
  /// components for both the case where return is considered a capture, and
  /// where it isn't.
  struct CaptureResult {
    /// Captured components excluding return captures.
    CaptureComponents WithoutRet;
    /// Captured components including return captures.
    CaptureComponents WithRet;
  };

  /// Return which components of the pointer may be captured.
  ///
  /// Only consider components that are part of \p Mask. Once \p StopFn on the
  /// accumulated components returns true, the traversal is aborted early. By
  /// default, this happens when *any* of the components in \p Mask are
  /// captured.
  ///
  /// This function only considers captures of the passed value via its def-use
  /// chain, without considering captures of values it may be based on, or
  /// implicit captures such as for external globals.
  /// @param V Pointer value whose captures are queried.
  /// @param Mask Capture components to consider.
  /// @param StopFn Predicate that aborts traversal when it returns true on the
  /// accumulated components.
  /// @param MaxUsesToExplore How many uses to explore before giving up; zero
  /// means use the default.
  /// @return Captured components with and without treating return as a capture.
  LLVM_ABI CaptureResult PointerMayBeCaptured(
      const Value *V, CaptureComponents Mask,
      function_ref<bool(CaptureComponents)> StopFn = capturesAnything,
      unsigned MaxUsesToExplore = 0);

  /// Return true if this pointer may be captured before a given instruction.
  ///
  /// The enclosing function is required to exist. If a DominatorTree is
  /// provided, only captures which happen before the given instruction are
  /// considered. This routine can be expensive, so consider caching the
  /// results. Captures by the provided instruction are considered if
  /// \p IncludeI is true.
  ///
  /// This function only considers captures of the passed value via its def-use
  /// chain, without considering captures of values it may be based on, or
  /// implicit captures such as for external globals.
  /// @param V Pointer value whose captures are queried.
  /// @param ReturnCaptures Whether returning the value (or part of it) counts
  /// as capturing it.
  /// @param I Instruction before which captures are considered.
  /// @param DT Optional dominator tree used to restrict captures to those
  /// before \p I.
  /// @param IncludeI Whether captures by \p I itself are considered.
  /// @param MaxUsesToExplore How many uses to explore before giving up; zero
  /// means use the default.
  /// @param LI Optional loop info used to prune reachability checks.
  /// @return True if the pointer may be captured before \p I.
  LLVM_ABI bool PointerMayBeCapturedBefore(const Value *V, bool ReturnCaptures,
                                           const Instruction *I,
                                           const DominatorTree *DT,
                                           bool IncludeI = false,
                                           unsigned MaxUsesToExplore = 0,
                                           const LoopInfo *LI = nullptr);

  /// Return which components of the pointer may be captured on the path to
  /// \p I.
  ///
  /// Only consider components that are part of \p Mask. Once \p StopFn on the
  /// accumulated components returns true, the traversal is aborted early. By
  /// default, this happens when *any* of the components in \p Mask are
  /// captured.
  ///
  /// This function only considers captures of the passed value via its def-use
  /// chain, without considering captures of values it may be based on, or
  /// implicit captures such as for external globals.
  /// @param V Pointer value whose captures are queried.
  /// @param ReturnCaptures Whether returning the value (or part of it) counts
  /// as capturing it.
  /// @param I Instruction before which captures are considered.
  /// @param DT Optional dominator tree used to restrict captures to those
  /// before \p I.
  /// @param IncludeI Whether captures by \p I itself are considered.
  /// @param Mask Capture components to consider.
  /// @param StopFn Predicate that aborts traversal when it returns true on the
  /// accumulated components.
  /// @param LI Optional loop info used to prune reachability checks.
  /// @param MaxUsesToExplore How many uses to explore before giving up; zero
  /// means use the default.
  /// @return Capture components that may be captured on the path to \p I.
  LLVM_ABI CaptureComponents PointerMayBeCapturedBefore(
      const Value *V, bool ReturnCaptures, const Instruction *I,
      const DominatorTree *DT, bool IncludeI, CaptureComponents Mask,
      function_ref<bool(CaptureComponents)> StopFn = capturesAnything,
      const LoopInfo *LI = nullptr, unsigned MaxUsesToExplore = 0);

  /// Find the earliest instruction that captures \p V in \p F.
  ///
  /// Also returns which components may be captured (by any use, not necessarily
  /// the earliest one). An instruction A is considered earlier than instruction
  /// B if A dominates B. If two escapes do not dominate each other, the
  /// terminator of the common dominator is chosen. If not all uses can be
  /// analyzed, the earliest escape is set to the first instruction in the
  /// function entry block. If \p V does not escape, nullptr is returned. Note
  /// that the caller of the function has to ensure that the instruction the
  /// result value is compared against is not in a cycle.
  ///
  /// Only consider components that are part of \p Mask.
  /// @param V Pointer value whose captures are queried.
  /// @param F Function in which captures are searched.
  /// @param DT Dominator tree used to order capturing instructions.
  /// @param Mask Capture components to consider.
  /// @param MaxUsesToExplore How many uses to explore before giving up; zero
  /// means use the default.
  /// @return Pair of the earliest capturing instruction (or nullptr) and the
  /// captured components.
  LLVM_ABI std::pair<Instruction *, CaptureResult>
  FindEarliestCapture(const Value *V, Function &F, const DominatorTree &DT,
                      CaptureComponents Mask, unsigned MaxUsesToExplore = 0);

  /// Capture information for a specific Use.
  struct UseCaptureInfo {
    /// Components captured by this use.
    CaptureComponents UseCC;
    /// Components captured by the return value of the user of this Use.
    CaptureComponents ResultCC;

    /// Construct capture info for a use.
    /// @param UseCC Components captured directly by this use.
    /// @param ResultCC Components captured by the return value of the user.
    UseCaptureInfo(CaptureComponents UseCC,
                   CaptureComponents ResultCC = CaptureComponents::None)
        : UseCC(UseCC), ResultCC(ResultCC) {}

    /// Return info for a use that captures only via its result.
    /// @return Capture info with no direct captures and all result captures.
    static UseCaptureInfo passthrough() {
      return UseCaptureInfo(CaptureComponents::None, CaptureComponents::All);
    }

    /// Return whether this use captures only via its result.
    /// @return True if the use captures nothing directly and everything via
    /// its result.
    bool isPassthrough() const {
      return capturesNothing(UseCC) && capturesAnything(ResultCC);
    }

    /// Return the union of the use and result capture components.
    /// @return Union of the use and result capture components.
    operator CaptureComponents() const { return UseCC | ResultCC; }
  };

  /// Callback interface for customizing PointerMayBeCaptured traversal.
  ///
  /// This callback is used in conjunction with PointerMayBeCaptured. In
  /// addition to the interface here, you'll need to provide your own getters
  /// to see whether anything was captured.
  struct LLVM_ABI CaptureTracker {
    /// Action returned from captures().
    enum Action {
      /// Stop the traversal.
      Stop,
      /// Continue traversal, and also follow the return value of the user if
      /// it has additional capture components (that is, if it has capture
      /// components in Ret that are not part of Other).
      Continue,
      /// Continue traversal, but do not follow the return value of the user,
      /// even if it has additional capture components. Should only be used if
      /// captures() has already taken the potential return captures into
      /// account.
      ContinueIgnoringReturn,
    };

    /// Destroy this CaptureTracker.
    virtual ~CaptureTracker();

    /// tooManyUses - The depth of traversal has breached a limit. There may be
    /// capturing instructions that will not be passed into captured().
    virtual void tooManyUses() = 0;

    /// Return true if this use of a derived value should be searched.
    ///
    /// This is the use of a value derived from the pointer. To prune the
    /// search (ie., assume that none of its users could possibly capture)
    /// return false. To search it, return true.
    ///
    /// U->getUser() is always an Instruction.
    /// @param U Use of a value derived from the pointer.
    /// @return True if the use should be searched; false to prune it.
    virtual bool shouldExplore(const Use *U);

    /// Use U directly captures CI.UseCC and additionally CI.ResultCC
    /// through the return value of the user of U.
    ///
    /// Return one of Stop, Continue or ContinueIgnoringReturn to control
    /// further traversal.
    /// @param U Use that captures the pointer.
    /// @param CI Capture components for this use and its user's return value.
    /// @return Action controlling whether and how traversal continues.
    virtual Action captured(const Use *U, UseCaptureInfo CI) = 0;
  };

  /// Determine what kind of capture behaviour \p U may exhibit.
  ///
  /// The returned UseCaptureInfo contains the components captured directly
  /// by the use (UseCC) and the components captured through the return value
  /// of the user (ResultCC).
  ///
  /// \p Base is the starting value of the capture analysis, which is
  /// relevant for address_is_null captures.
  /// @param U Use whose capture kind is classified.
  /// @param Base Starting value of the capture analysis.
  /// @return Capture components for the use and for the user's return value.
  LLVM_ABI UseCaptureInfo DetermineUseCaptureKind(const Use &U,
                                                  const Value *Base);

  /// Visit a pointer and derived values to find capturing uses.
  ///
  /// This feeds results into and is controlled by the CaptureTracker object.
  ///
  /// This function only considers captures of the passed value via its def-use
  /// chain, without considering captures of values it may be based on, or
  /// implicit captures such as for external globals.
  /// @param V Pointer value whose uses are visited.
  /// @param Tracker Callback that receives capturing uses and controls
  /// traversal.
  /// @param MaxUsesToExplore How many uses to explore before giving up; zero
  /// means use the default.
  LLVM_ABI void PointerMayBeCaptured(const Value *V, CaptureTracker *Tracker,
                                     unsigned MaxUsesToExplore = 0);
} // end namespace llvm

#endif
