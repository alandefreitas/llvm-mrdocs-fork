//===- AssumeBundleQueries.h - utilis to query assume bundles ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contain tools to query into assume bundles. assume bundles can be
// built using utilities from Transform/Utils/AssumeBundleBuilder.h
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ASSUMEBUNDLEQUERIES_H
#define LLVM_ANALYSIS_ASSUMEBUNDLEQUERIES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class AssumptionCache;
class DominatorTree;
class Instruction;

/// Index of elements in the operand bundle.
/// If the element exist it is guaranteed to be what is specified in this enum
/// but it may not exist.
enum AssumeBundleArg {
  /// Value for which the attribute holds.
  ABA_WasOn = 0,
  /// Optional attribute argument (e.g. alignment amount).
  ABA_Argument = 1,
};

/// DenseMapInfo specialization for Attribute::AttrKind.
template <> struct DenseMapInfo<Attribute::AttrKind> {
  /// Hash an attribute kind for DenseMap.
  /// @param AK Attribute kind to hash.
  /// @return Hash value for \p AK.
  static unsigned getHashValue(Attribute::AttrKind AK) {
    return hash_combine(AK);
  }
  /// Compare two attribute kinds for equality.
  /// @param LHS Left-hand attribute kind.
  /// @param RHS Right-hand attribute kind.
  /// @return True if \p LHS and \p RHS are the same attribute kind.
  static bool isEqual(Attribute::AttrKind LHS, Attribute::AttrKind RHS) {
    return LHS == RHS;
  }
};

/// Key pairing a value with an attribute kind for retained knowledge.
///
/// The map Key contains the Value on for which the attribute is valid and
/// the Attribute that is valid for that value.
/// If the Attribute is not on any value, the Value is nullptr.
using RetainedKnowledgeKey = std::pair<Value *, Attribute::AttrKind>;

/// Inclusive minimum and maximum bounds for a retained knowledge value range.
struct MinMax {
  /// Lower bound of the value range.
  uint64_t Min;
  /// Upper bound of the value range.
  uint64_t Max;
};

/// Map from llvm.assume intrinsics to encoded knowledge value ranges.
///
/// How the value range is interpreted depends on the RetainedKnowledgeKey
/// that was used to get this out of the RetainedKnowledgeMap.
using Assume2KnowledgeMap = DenseMap<AssumeInst *, MinMax>;

/// Map from retained-knowledge keys to assume intrinsics and their ranges.
using RetainedKnowledgeMap =
    DenseMap<RetainedKnowledgeKey, Assume2KnowledgeMap>;

/// Populate a map with all knowledge from an llvm.assume's operand bundles.
///
/// This should be used when many queries are going to be made on the same
/// llvm.assume. String attributes are not inserted in the map. If the IR
/// changes the map will be outdated.
/// @param Assume The llvm.assume whose operand bundles are scanned.
/// @param Result Map to populate with the extracted knowledge.
LLVM_ABI void fillMapFromAssume(AssumeInst &Assume,
                                RetainedKnowledgeMap &Result);

/// One piece of knowledge from an llvm.assume operand bundle.
///
/// AttrKind is the property that holds. WasOn if not null is that Value for
/// which AttrKind holds. ArgValue is optionally an argument of the attribute.
/// For example if we know that %P has an alignment of at least four:
///  - AttrKind will be Attribute::Alignment.
///  - WasOn will be %P.
///  - ArgValue will be 4.
struct RetainedKnowledge {
  /// Attribute kind that is known to hold.
  Attribute::AttrKind AttrKind = Attribute::None;
  /// Optional numeric argument for the attribute (e.g. alignment).
  uint64_t ArgValue = 0;
  /// Optional IR value argument associated with the attribute.
  Value *IRArgValue = nullptr;
  /// Value to which the attribute applies, or nullptr if none.
  Value *WasOn = nullptr;
  /// Construct retained knowledge for an attribute and optional value.
  /// @param AttrKind Attribute kind that holds.
  /// @param ArgValue Optional numeric attribute argument.
  /// @param WasOn Value the attribute applies to, or nullptr.
  RetainedKnowledge(Attribute::AttrKind AttrKind = Attribute::None,
                    uint64_t ArgValue = 0, Value *WasOn = nullptr)
      : AttrKind(AttrKind), ArgValue(ArgValue), WasOn(WasOn) {}
  /// Return true if this knowledge equals \p Other.
  /// @param Other Knowledge to compare against.
  /// @return True if the two knowledge values are equal.
  bool operator==(RetainedKnowledge Other) const {
    return AttrKind == Other.AttrKind && WasOn == Other.WasOn &&
           ArgValue == Other.ArgValue && IRArgValue == Other.IRArgValue;
  }
  /// Return true if this knowledge differs from \p Other.
  /// @param Other Knowledge to compare against.
  /// @return True if the two knowledge values differ.
  bool operator!=(RetainedKnowledge Other) const { return !(*this == Other); }
  /// Compare by ArgValue among otherwise equal attributes.
  ///
  /// This is only intended for use in std::min/std::max between attribute that
  /// only differ in ArgValue.
  /// @param Other Knowledge to compare against.
  /// @return True if this ArgValue is less than \p Other's.
  bool operator<(RetainedKnowledge Other) const {
    assert(((AttrKind == Other.AttrKind && WasOn == Other.WasOn) ||
            AttrKind == Attribute::None || Other.AttrKind == Attribute::None) &&
           "This is only intend for use in min/max to select the best for "
           "RetainedKnowledge that is otherwise equal");
    return ArgValue < Other.ArgValue;
  }
  /// Return true if this knowledge is non-empty.
  /// @return True if AttrKind is not Attribute::None.
  operator bool() const { return AttrKind != Attribute::None; }
  /// Return an empty RetainedKnowledge.
  /// @return A default-constructed empty RetainedKnowledge.
  static RetainedKnowledge none() { return RetainedKnowledge{}; }
};

/// Tag in operand bundle indicating that this bundle should be ignored.
constexpr StringRef IgnoreBundleTag = "ignore";

/// Return true if an llvm.assume's operand bundles hold no useful knowledge.
///
/// This is true when:
///  - The operand bundle is empty
///  - The operand bundle only contains information about dropped values or
///    constant folded values.
///
/// The argument to the call of llvm.assume may still be useful even if the
/// function returned true.
/// @param Assume The llvm.assume to inspect.
/// @return True if the operand bundles hold no useful knowledge.
LLVM_ABI bool isAssumeWithEmptyBundle(const AssumeInst &Assume);

/// Return a valid Knowledge associated to the Use U if its Attribute kind is
/// in AttrKinds.
/// @param U Use whose associated assume knowledge is queried.
/// @param AttrKinds Attribute kinds to consider.
/// @return Matching retained knowledge, or empty if none applies.
LLVM_ABI RetainedKnowledge
getKnowledgeFromUse(const Use *U, ArrayRef<Attribute::AttrKind> AttrKinds);

/// Return a valid Knowledge associated to the Value V if its Attribute kind is
/// in AttrKinds and it matches the Filter.
/// @param V Value whose assume knowledge is queried.
/// @param AttrKinds Attribute kinds to consider.
/// @param AC Cache of assumptions for the function.
/// @param Filter Predicate that must accept a candidate knowledge.
/// @return Matching retained knowledge, or empty if none applies.
LLVM_ABI RetainedKnowledge getKnowledgeForValue(
    const Value *V, ArrayRef<Attribute::AttrKind> AttrKinds,
    AssumptionCache &AC,
    function_ref<bool(RetainedKnowledge, Instruction *,
                      const CallBase::BundleOpInfo *)>
        Filter = [](auto...) { return true; });

/// Return a valid Knowledge associated to the Value V if its Attribute kind is
/// in AttrKinds and the knowledge is suitable to be used in the context of
/// CtxI.
/// @param V Value whose assume knowledge is queried.
/// @param AttrKinds Attribute kinds to consider.
/// @param AC Cache of assumptions for the function.
/// @param CtxI Instruction defining the context where knowledge must be valid.
/// @param DT Optional dominator tree used for context checks.
/// @return Matching retained knowledge valid at \p CtxI, or empty if none.
LLVM_ABI RetainedKnowledge getKnowledgeValidInContext(
    const Value *V, ArrayRef<Attribute::AttrKind> AttrKinds,
    AssumptionCache &AC, const Instruction *CtxI,
    const DominatorTree *DT = nullptr);

/// This extracts the Knowledge from an element of an operand bundle.
/// This is mostly for use in the assume builder.
/// @param Assume The llvm.assume that owns the operand bundle.
/// @param BOI Operand-bundle element to extract knowledge from.
/// @return Retained knowledge extracted from the bundle element.
LLVM_ABI RetainedKnowledge
getKnowledgeFromBundle(AssumeInst &Assume, const CallBase::BundleOpInfo &BOI);

} // namespace llvm

#endif
