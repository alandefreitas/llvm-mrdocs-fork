//===- OpenMP/OMPContext.h ----- OpenMP context helper functions  - C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides helper functions and classes to deal with OpenMP
/// contexts as used by `[begin/end] declare variant` and `metadirective`.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_OPENMP_OMPCONTEXT_H
#define LLVM_FRONTEND_OPENMP_OMPCONTEXT_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Frontend/OpenMP/OMPConstants.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Triple;
namespace omp {

/// OpenMP Context related IDs and helpers
///
///{

/// IDs for all OpenMP context selector trait sets (construct/device/...).
enum class TraitSet {
#define OMP_TRAIT_SET(Enum, ...) Enum,
#include "llvm/Frontend/OpenMP/OMPKinds.def"
};

/// IDs for all OpenMP context selector trait (device={kind/isa...}/...).
enum class TraitSelector {
#define OMP_TRAIT_SELECTOR(Enum, ...) Enum,
#include "llvm/Frontend/OpenMP/OMPKinds.def"
};

/// IDs for all OpenMP context trait properties (host/gpu/bsc/llvm/...)
enum class TraitProperty {
#define OMP_TRAIT_PROPERTY(Enum, ...) Enum,
#define OMP_LAST_TRAIT_PROPERTY(Enum) Last = Enum
#include "llvm/Frontend/OpenMP/OMPKinds.def"
};

/// Parse \p Str and return the trait set it matches or TraitSet::invalid.
/// \param Str Trait set name to parse.
/// \return The matching TraitSet, or TraitSet::invalid if none matches.
LLVM_ABI TraitSet getOpenMPContextTraitSetKind(StringRef Str);

/// Return the trait set for which \p Selector is a selector.
/// \param Selector Trait selector whose enclosing set is returned.
/// \return The TraitSet that contains \p Selector.
LLVM_ABI TraitSet getOpenMPContextTraitSetForSelector(TraitSelector Selector);

/// Return the trait set for which \p Property is a property.
/// \param Property Trait property whose enclosing set is returned.
/// \return The TraitSet that contains \p Property.
LLVM_ABI TraitSet getOpenMPContextTraitSetForProperty(TraitProperty Property);

/// Return a textual representation of the trait set \p Kind.
/// \param Kind Trait set whose name is returned.
/// \return The name of trait set \p Kind.
LLVM_ABI StringRef getOpenMPContextTraitSetName(TraitSet Kind);

/// Parse \p Str and return the trait set it matches or
/// TraitSelector::invalid.
/// \param Str Trait selector name to parse.
/// \param Set Trait set that scopes the selector lookup.
/// \return The matching TraitSelector, or TraitSelector::invalid if none
/// matches.
LLVM_ABI TraitSelector getOpenMPContextTraitSelectorKind(StringRef Str,
                                                         TraitSet Set);

/// Return the trait selector for which \p Property is a property.
/// \param Property Trait property whose enclosing selector is returned.
/// \return The TraitSelector that contains \p Property.
LLVM_ABI TraitSelector
getOpenMPContextTraitSelectorForProperty(TraitProperty Property);

/// Return a textual representation of the trait selector \p Kind.
/// \param Kind Trait selector whose name is returned.
/// \return The name of trait selector \p Kind.
LLVM_ABI StringRef getOpenMPContextTraitSelectorName(TraitSelector Kind);

/// Parse \p Str and return the trait property it matches in the set \p Set and
/// selector \p Selector or TraitProperty::invalid.
/// \param Set Trait set that scopes the property lookup.
/// \param Selector Trait selector that scopes the property lookup.
/// \param Str Trait property name to parse.
/// \return The matching TraitProperty, or TraitProperty::invalid if none
/// matches.
LLVM_ABI TraitProperty getOpenMPContextTraitPropertyKind(TraitSet Set,
                                                         TraitSelector Selector,
                                                         StringRef Str);

/// Return the trait property for a singleton selector \p Selector.
/// \param Selector Singleton trait selector whose property is returned.
/// \return The TraitProperty associated with singleton selector \p Selector.
LLVM_ABI TraitProperty
getOpenMPContextTraitPropertyForSelector(TraitSelector Selector);

/// Return a textual representation of the trait property \p Kind.
///
/// The result might be the raw string we parsed (\p RawString) if we do not
/// translate the property into a (distinct) enum.
/// \param Kind Trait property whose name is returned.
/// \param RawString Parsed spelling used when \p Kind has no distinct enum.
/// \return The name of trait property \p Kind, or \p RawString when there is
/// no distinct enum spelling.
LLVM_ABI StringRef getOpenMPContextTraitPropertyName(TraitProperty Kind,
                                                     StringRef RawString);

/// Return a textual representation of the trait property \p Kind with selector
/// and set name included.
/// \param Kind Trait property whose full name is returned.
/// \return The full name of trait property \p Kind, including set and
/// selector.
LLVM_ABI StringRef getOpenMPContextTraitPropertyFullName(TraitProperty Kind);

/// Return a string listing all trait sets.
/// \return A string that lists every OpenMP context trait set.
LLVM_ABI std::string listOpenMPContextTraitSets();

/// Return a string listing all trait selectors for \p Set.
/// \param Set Trait set whose selectors are listed.
/// \return A string that lists every trait selector in \p Set.
LLVM_ABI std::string listOpenMPContextTraitSelectors(TraitSet Set);

/// Return a string listing all trait properties for \p Set and \p Selector.
/// \param Set Trait set that scopes the listed properties.
/// \param Selector Trait selector whose properties are listed.
/// \return A string that lists every trait property for \p Set and \p Selector.
LLVM_ABI std::string listOpenMPContextTraitProperties(TraitSet Set,
                                                      TraitSelector Selector);
///}

/// Return true if \p Selector can be nested in \p Set.
///
/// Also sets \p AllowsTraitScore and \p RequiresProperty to true/false if the
/// user can specify a score for properties in \p Selector and if the
/// \p Selector requires at least one property.
/// \param Selector Trait selector to validate.
/// \param Set Trait set that would enclose \p Selector.
/// \param AllowsTraitScore Set to whether properties in \p Selector may have a
/// score.
/// \param RequiresProperty Set to whether \p Selector needs at least one
/// property.
/// \return True if \p Selector is a valid nested selector of \p Set.
LLVM_ABI bool isValidTraitSelectorForTraitSet(TraitSelector Selector,
                                              TraitSet Set,
                                              bool &AllowsTraitScore,
                                              bool &RequiresProperty);

/// Return true if \p Property can be nested in \p Selector and \p Set.
/// \param Property Trait property to validate.
/// \param Selector Trait selector that would enclose \p Property.
/// \param Set Trait set that would enclose \p Selector.
/// \return True if \p Property is valid under \p Selector and \p Set.
LLVM_ABI bool isValidTraitPropertyForTraitSetAndSelector(TraitProperty Property,
                                                         TraitSelector Selector,
                                                         TraitSet Set);

/// Variant match information for required traits, scores, and construct nesting.
///
/// Describes the required traits and how they are scored (via the ScoresMap).
/// In addition, the required construct nesting is described as well.
struct VariantMatchInfo {
  /// Add \p Property to the required trait set, recording score and nesting.
  ///
  /// \p RawString is the string we parsed and derived \p Property from. If
  /// \p Score is not null, it is recorded as well. If \p Property is in the
  /// `construct` set it is recorded in-order in the ConstructTraits as well.
  /// \param Property Trait property to require.
  /// \param RawString Parsed spelling used to derive \p Property.
  /// \param Score Optional score associated with \p Property.
  void addTrait(TraitProperty Property, StringRef RawString,
                APInt *Score = nullptr) {
    addTrait(getOpenMPContextTraitSetForProperty(Property), Property, RawString,
             Score);
  }
  /// Add \p Property from set \p Set to the required trait set.
  ///
  /// \p RawString is the string we parsed and derived \p Property from. If
  /// \p Score is not null, it is recorded as well. If \p Set is the `construct`
  /// set it is recorded in-order in the ConstructTraits as well.
  /// \param Set Trait set that contains \p Property.
  /// \param Property Trait property to require.
  /// \param RawString Parsed spelling used to derive \p Property.
  /// \param Score Optional score associated with \p Property.
  void addTrait(TraitSet Set, TraitProperty Property, StringRef RawString,
                APInt *Score = nullptr) {
    if (Score)
      ScoreMap[Property] = *Score;

    // Special handling for `device={isa(...)}` as we do not match the enum but
    // the raw string.
    if (Property == TraitProperty::device_isa___ANY)
      ISATraits.push_back(RawString);
    if (Property == TraitProperty::target_device_isa___ANY)
      ISATraits.push_back(RawString);

    RequiredTraits.set(unsigned(Property));
    if (Set == TraitSet::construct)
      ConstructTraits.push_back(Property);
  }

  /// Bit vector of required trait properties indexed by TraitProperty.
  BitVector RequiredTraits = BitVector(unsigned(TraitProperty::Last) + 1);
  /// Raw ISA trait strings that must match via matchesISATrait.
  SmallVector<StringRef, 8> ISATraits;
  /// Ordered construct-set properties describing required nesting.
  SmallVector<TraitProperty, 8> ConstructTraits;
  /// Optional scores associated with required trait properties.
  SmallDenseMap<TraitProperty, APInt> ScoreMap;
};

/// OpenMP context for a source location's active and construct traits.
///
/// The context for a source location is made up of active property traits,
/// e.g., device={kind(host)}, and constructs traits which describe the nesting
/// in OpenMP constructs at the location.
struct OMPContext {
  /// Construct an OpenMP context for the given compilation and target.
  /// \param IsDeviceCompilation Whether code is generated for a device.
  /// \param TargetTriple Triple of the primary compilation target.
  /// \param TargetOffloadTriple Triple of the offload target.
  /// \param DeviceNum OpenMP device number for the context.
  LLVM_ABI OMPContext(bool IsDeviceCompilation, Triple TargetTriple,
                      Triple TargetOffloadTriple, int DeviceNum);
  /// Destroy the OpenMP context.
  virtual ~OMPContext() = default;

  /// Add \p Property to the active trait set.
  /// \param Property Trait property to mark active.
  void addTrait(TraitProperty Property) {
    addTrait(getOpenMPContextTraitSetForProperty(Property), Property);
  }
  /// Add \p Property from set \p Set to the active trait set.
  /// \param Set Trait set that contains \p Property.
  /// \param Property Trait property to mark active.
  void addTrait(TraitSet Set, TraitProperty Property) {
    ActiveTraits.set(unsigned(Property));
    if (Set == TraitSet::construct)
      ConstructTraits.push_back(Property);
  }

  /// Return true if the parsed ISA trait string matches this context.
  ///
  /// The trait is described as the string that got parsed and it depends on
  /// the target and context if this matches or not.
  /// \param ISATrait Parsed ISA trait string to match against the target.
  /// \return True if \p ISATrait matches this context; the base implementation
  /// always returns false.
  virtual bool matchesISATrait(StringRef ISATrait) const { return false; }

  /// Bit vector of active trait properties indexed by TraitProperty.
  BitVector ActiveTraits = BitVector(unsigned(TraitProperty::Last) + 1);
  /// Ordered construct-set properties describing nesting at the location.
  SmallVector<TraitProperty, 8> ConstructTraits;
};

/// Return true if \p VMI is applicable in OpenMP context \p Ctx.
///
/// That is, all traits required by \p VMI are available in \p Ctx. If
/// \p DeviceOrImplementationSetOnly is true, only the device and implementation
/// selector set, if present, are checked. Note that we still honor extension
/// traits provided by the user.
/// \param VMI Variant match info whose required traits are checked.
/// \param Ctx OpenMP context to test applicability against.
/// \param DeviceOrImplementationSetOnly When true, only device and
/// implementation selector sets are considered.
/// \return True if \p VMI's required traits are available in \p Ctx.
LLVM_ABI bool
isVariantApplicableInContext(const VariantMatchInfo &VMI, const OMPContext &Ctx,
                             bool DeviceOrImplementationSetOnly = false);

/// Return the index (into \p VMIs) of the variant with the highest score
/// from the ones applicable in \p Ctx. See llvm::isVariantApplicableInContext.
/// \param VMIs Candidate variant match infos to rank.
/// \param Ctx OpenMP context used to filter and score candidates.
/// \return The index into \p VMIs of the highest-scoring applicable variant.
LLVM_ABI int
getBestVariantMatchForContext(const SmallVectorImpl<VariantMatchInfo> &VMIs,
                              const OMPContext &Ctx);

} // namespace omp

/// DenseMapInfo specialization for omp::TraitProperty keys.
template <> struct DenseMapInfo<omp::TraitProperty> {
  /// Compute a hash value for trait property \p val.
  /// \param val Trait property key to hash.
  /// \return A hash value derived from \p val.
  static unsigned getHashValue(omp::TraitProperty val) {
    return std::hash<unsigned>{}(unsigned(val));
  }
  /// Return true if \p LHS and \p RHS are the same trait property.
  /// \param LHS Left-hand trait property.
  /// \param RHS Right-hand trait property.
  /// \return True if \p LHS and \p RHS are equal.
  static bool isEqual(omp::TraitProperty LHS, omp::TraitProperty RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm
#endif // LLVM_FRONTEND_OPENMP_OMPCONTEXT_H
