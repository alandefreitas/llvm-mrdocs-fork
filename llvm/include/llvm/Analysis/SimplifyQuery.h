//===-- SimplifyQuery.h - Context for simplifications -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SIMPLIFYQUERY_H
#define LLVM_ANALYSIS_SIMPLIFYQUERY_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class AssumptionCache;
class DomConditionCache;
class DominatorTree;
class TargetLibraryInfo;

/// Query wrapper for instruction metadata and wrap flags.
///
/// InstrInfoQuery provides an interface to query additional information for
/// instructions like metadata or keywords like nsw, which provides conservative
/// results if the users specified it is safe to use.
struct InstrInfoQuery {
  /// Construct a query that optionally uses instruction-level information.
  /// @param UMD Whether to honor metadata and wrap flags on instructions.
  InstrInfoQuery(bool UMD) : UseInstrInfo(UMD) {}
  /// Construct a query that uses instruction-level information.
  InstrInfoQuery() = default;
  /// Whether metadata and wrap flags on instructions may be used.
  bool UseInstrInfo = true;

  /// Return metadata of kind \p KindID on \p I, or null if info is disabled.
  /// @param I Instruction to query.
  /// @param KindID Metadata kind identifier.
  /// @return Metadata of kind \p KindID on \p I, or null if info is disabled.
  MDNode *getMetadata(const Instruction *I, unsigned KindID) const {
    if (UseInstrInfo)
      return I->getMetadata(KindID);
    return nullptr;
  }

  /// Return the nuw flag of \p Op, or false if instruction info is disabled.
  /// @param Op Instruction to query for the nuw flag.
  /// @return True if \p Op has the nuw flag when info is enabled; otherwise false.
  template <class InstT> bool hasNoUnsignedWrap(const InstT *Op) const {
    if (UseInstrInfo)
      return Op->hasNoUnsignedWrap();
    return false;
  }

  /// Return the nsw flag of \p Op, or false if instruction info is disabled.
  /// @param Op Instruction to query for the nsw flag.
  /// @return True if \p Op has the nsw flag when info is enabled; otherwise false.
  template <class InstT> bool hasNoSignedWrap(const InstT *Op) const {
    if (UseInstrInfo)
      return Op->hasNoSignedWrap();
    return false;
  }

  /// Return the exact flag of \p Op, or false if instruction info is disabled.
  /// @param Op Binary operator to query for the exact flag.
  /// @return True if \p Op is exact when info is enabled; otherwise false.
  bool isExact(const BinaryOperator *Op) const {
    if (UseInstrInfo && isa<PossiblyExactOperator>(Op))
      return cast<PossiblyExactOperator>(Op)->isExact();
    return false;
  }

  /// Return the nsz flag of \p Op, or false if instruction info is disabled.
  /// @param Op Instruction to query for the nsz flag.
  /// @return True if \p Op has the nsz flag when info is enabled; otherwise false.
  template <class InstT> bool hasNoSignedZeros(const InstT *Op) const {
    if (UseInstrInfo)
      return Op->hasNoSignedZeros();
    return false;
  }
};

/// Evaluate query assuming this condition holds.
struct CondContext {
  /// Condition assumed to hold for this query.
  Value *Cond;
  /// When true, assume Cond is false rather than true.
  bool Invert = false;
  /// Values whose known bits may be affected by Cond.
  SmallPtrSet<Value *, 4> AffectedValues;

  /// Construct a context that assumes \p Cond holds.
  /// @param Cond Condition value assumed to be true.
  CondContext(Value *Cond) : Cond(Cond) {}
};

/// Analyses and flags used as context for instruction simplification.
struct SimplifyQuery {
  /// Data layout used for type sizes and pointer widths.
  const DataLayout &DL;
  /// Optional target library info for recognizing library calls.
  const TargetLibraryInfo *TLI = nullptr;
  /// Optional dominator tree for context-sensitive analysis.
  const DominatorTree *DT = nullptr;
  /// Optional cache of \@llvm.assume calls.
  AssumptionCache *AC = nullptr;
  /// Optional instruction providing local simplification context.
  const Instruction *CxtI = nullptr;
  /// Optional cache of dominating branch conditions.
  const DomConditionCache *DC = nullptr;
  /// Optional condition assumed to hold for this query.
  const CondContext *CC = nullptr;

  /// Instruction-info query used by this simplification.
  ///
  /// Wrapper to query additional information for instructions like metadata or
  /// keywords like nsw, which provides conservative results if those cannot
  /// be safely used.
  const InstrInfoQuery IIQ;

  /// Whether simplifications may pick a value for uses of undef.
  ///
  /// Controls whether simplifications are allowed to constrain the range of
  /// possible values for uses of undef. If it is false, simplifications are not
  /// allowed to assume a particular value for a use of undef for example.
  bool CanUseUndef = true;
  /// Whether ephemeral values are valid as a simplification context.
  bool AllowEphemerals = false;

  /// Construct a query from a data layout and optional context instruction.
  /// @param DL Data layout used for type sizes and pointer widths.
  /// @param CXTI Optional instruction providing local simplification context.
  SimplifyQuery(const DataLayout &DL, const Instruction *CXTI = nullptr)
      : DL(DL), CxtI(CXTI) {}

  /// Construct a query with target library info and optional analyses.
  /// @param DL Data layout used for type sizes and pointer widths.
  /// @param TLI Optional target library info for recognizing library calls.
  /// @param DT Optional dominator tree for context-sensitive analysis.
  /// @param AC Optional cache of \@llvm.assume calls.
  /// @param CXTI Optional instruction providing local simplification context.
  /// @param UseInstrInfo Whether to honor metadata and wrap flags.
  /// @param CanUseUndef Whether simplifications may pick a value for undef.
  /// @param DC Optional cache of dominating branch conditions.
  SimplifyQuery(const DataLayout &DL, const TargetLibraryInfo *TLI,
                const DominatorTree *DT = nullptr,
                AssumptionCache *AC = nullptr,
                const Instruction *CXTI = nullptr, bool UseInstrInfo = true,
                bool CanUseUndef = true, const DomConditionCache *DC = nullptr)
      : DL(DL), TLI(TLI), DT(DT), AC(AC), CxtI(CXTI), DC(DC), IIQ(UseInstrInfo),
        CanUseUndef(CanUseUndef) {}

  /// Construct a query with a dominator tree and optional analyses.
  /// @param DL Data layout used for type sizes and pointer widths.
  /// @param DT Optional dominator tree for context-sensitive analysis.
  /// @param AC Optional cache of \@llvm.assume calls.
  /// @param CXTI Optional instruction providing local simplification context.
  /// @param UseInstrInfo Whether to honor metadata and wrap flags.
  /// @param CanUseUndef Whether simplifications may pick a value for undef.
  SimplifyQuery(const DataLayout &DL, const DominatorTree *DT,
                AssumptionCache *AC = nullptr,
                const Instruction *CXTI = nullptr, bool UseInstrInfo = true,
                bool CanUseUndef = true)
      : DL(DL), DT(DT), AC(AC), CxtI(CXTI), IIQ(UseInstrInfo),
        CanUseUndef(CanUseUndef) {}

  /// Return a copy of this query using \p I as the context instruction.
  /// @param I Instruction to use as the new context.
  /// @return A copy of this query using \p I as the context instruction.
  SimplifyQuery getWithInstruction(const Instruction *I) const {
    SimplifyQuery Copy(*this);
    Copy.CxtI = I;
    return Copy;
  }
  /// Return a copy of this query that forbids assuming a value for undef.
  /// @return A copy of this query that forbids assuming a value for undef.
  SimplifyQuery getWithoutUndef() const {
    SimplifyQuery Copy(*this);
    Copy.CanUseUndef = false;
    return Copy;
  }
  /// Return a copy of this query with ephemeral values allowed as specified.
  /// @param AllowEphemerals Whether ephemeral values are a valid context.
  /// @return A copy of this query with \p AllowEphemerals applied.
  SimplifyQuery allowEphemerals(bool AllowEphemerals) const {
    SimplifyQuery Copy(*this);
    Copy.AllowEphemerals = AllowEphemerals;
    return Copy;
  }

  /// If CanUseUndef is true, returns whether \p V is undef.
  /// Otherwise always return false.
  /// @param V Value to test for undef.
  /// @return True if \p V is undef when CanUseUndef is true; otherwise false.
  LLVM_ABI bool isUndefValue(Value *V) const;

  /// Return a copy of this query with no dominating-condition cache.
  /// @return A copy of this query with no dominating-condition cache.
  SimplifyQuery getWithoutDomCondCache() const {
    SimplifyQuery Copy(*this);
    Copy.DC = nullptr;
    return Copy;
  }

  /// Return a copy of this query using \p CC as the assumed condition.
  /// @param CC Condition context to attach to the query.
  /// @return A copy of this query with \p CC as the assumed condition.
  SimplifyQuery getWithCondContext(const CondContext &CC) const {
    SimplifyQuery Copy(*this);
    Copy.CC = &CC;
    return Copy;
  }

  /// Return a copy of this query with no assumed condition context.
  /// @return A copy of this query with no assumed condition context.
  SimplifyQuery getWithoutCondContext() const {
    SimplifyQuery Copy(*this);
    Copy.CC = nullptr;
    return Copy;
  }
};

} // end namespace llvm

#endif
