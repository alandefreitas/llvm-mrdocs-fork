//===-- OMPDescriptors.h - OpenMP descriptors --------------------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions and declarations of OpenMP elements.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_OPENMP_OMPDESCRIPTORS_H
#define LLVM_FRONTEND_OPENMP_OMPDESCRIPTORS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Frontend/OpenMP/OMP.h"

namespace llvm::omp {
/// IDs for OpenMP clause and modifier properties used by descriptor tables.
enum class Property {
#define GEN_OMP_PROPERTY_ENUMS
#include "llvm/Frontend/OpenMP/OMPDescriptors.h.inc"
#undef GEN_OMP_PROPERTY_ENUMS
};

static constexpr size_t Property_enumSize =
    llvm::to_underlying(Property::Last_) -
    llvm::to_underlying(Property::First_) + 1;

/// IDs for OpenMP clause modifiers used by descriptor tables.
enum class Modifier {
#define GEN_OMP_MODIFIER_ENUMS
#include "llvm/Frontend/OpenMP/OMPDescriptors.h.inc"
#undef GEN_OMP_MODIFIER_ENUMS
};

static constexpr size_t Modifier_enumSize =
    llvm::to_underlying(Modifier::Last_) -
    llvm::to_underlying(Modifier::First_) + 1;

/// Set of OpenMP property identifiers.
using Properties = EnumSet<Property, Property_enumSize>;
/// Set of OpenMP modifier identifiers.
using Modifiers = EnumSet<Modifier, Modifier_enumSize>;

/// Set of OpenMP clause identifiers.
using Clauses = llvm::omp::ClauseSet;
/// Set of OpenMP directive identifiers.
using Directives = llvm::omp::DirectiveSet;

/// Types that describe OpenMP clauses and modifiers by language version.
namespace descriptor {
/// Versioned detail records stored inside OpenMP descriptors.
namespace details {
/// Shared fields present on every versioned OpenMP descriptor entry.
struct Base {
  /// Properties that apply to this descriptor for a given OpenMP version.
  Properties Props;
};

/// Versioned details describing an OpenMP clause.
struct Clause : public Base {
  /// Spelling of the clause name in source.
  StringRef Spelling;
  /// Directives that may carry this clause.
  Directives Dirs;
  /// Source languages in which this clause is available.
  SourceLanguage Langs;
  /// Modifiers that may appear on this clause.
  Modifiers Mods;
};

/// Versioned details describing an OpenMP clause modifier.
struct Modifier : public Base {
  /// Clauses that may carry this modifier.
  Clauses Cls;
};
} // namespace details

/// Map from OpenMP language version to a details record.
template <typename DetailsTy> using DetailsMap = DenseMap<unsigned, DetailsTy>;

/// Named, versioned description of an OpenMP language element.
template <typename DetailsTy> struct Descriptor {
  /// Copy-construct a descriptor.
  /// @param Other Descriptor to copy.
  Descriptor(const Descriptor &Other) = default;
  /// Move-construct a descriptor.
  /// @param Other Descriptor to move from.
  Descriptor(Descriptor &&Other) = default;
  /// Construct a descriptor with name \p N and versioned details \p D.
  /// @param N Canonical name of the described OpenMP element.
  /// @param D Map from OpenMP version to details for that version.
  Descriptor(StringRef N, DetailsMap<DetailsTy> &&D)
      : Name(N), Details(std::move(D)) {}

  /// Return the canonical name of this OpenMP element.
  /// @return Canonical name of the described OpenMP element.
  StringRef getName() const { return Name; }
  /// Return the map from OpenMP version to details.
  /// @return Map from OpenMP language version to details for that version.
  const DetailsMap<DetailsTy> &getDetails() const { return Details; }

  /// Return the OpenMP versions for which this descriptor has details.
  /// @return OpenMP versions that have a details entry in this descriptor.
  SmallVector<unsigned> getVersions() const {
    SmallVector<unsigned> Vs;
    for (unsigned V : llvm::omp::getOpenMPVersions()) {
      if (auto F = Details.find(V); F != Details.end())
        Vs.push_back(V);
    }
    return Vs;
  }

private:
  StringRef Name;

protected:
  /// Map from OpenMP language version to the corresponding details record.
  DetailsMap<DetailsTy> Details;
};

/// Descriptor for an OpenMP clause and its versioned attributes.
struct Clause : public Descriptor<details::Clause> {
  /// Alias for the base descriptor type.
  using Base = Descriptor<details::Clause>;
  /// Inherit constructors from \c Base.
  using Base::Base;
  /// Return the properties of this clause for OpenMP version \p V.
  /// @param V OpenMP language version to query.
  /// @return Properties that apply to this clause for version \p V.
  LLVM_ABI Properties getProperties(unsigned V) const;
  /// Return the directives that may carry this clause for version \p V.
  /// @param V OpenMP language version to query.
  /// @return Directives that may carry this clause for version \p V.
  LLVM_ABI Directives getDirectives(unsigned V) const;
  /// Return the modifiers allowed on this clause for version \p V.
  /// @param V OpenMP language version to query.
  /// @return Modifiers allowed on this clause for version \p V.
  LLVM_ABI Modifiers getModifiers(unsigned V) const;
};

/// Descriptor for an OpenMP clause modifier and its versioned attributes.
struct Modifier : public Descriptor<details::Modifier> {
  /// Alias for the base descriptor type.
  using Base = Descriptor<details::Modifier>;
  /// Inherit constructors from \c Base.
  using Base::Base;
  /// Return the properties of this modifier for OpenMP version \p V.
  /// @param V OpenMP language version to query.
  /// @return Properties that apply to this modifier for version \p V.
  LLVM_ABI Properties getProperties(unsigned V) const;
  /// Return the clauses that may carry this modifier for version \p V.
  /// @param V OpenMP language version to query.
  /// @return Clauses that may carry this modifier for version \p V.
  LLVM_ABI Clauses getClauses(unsigned V) const;
};
} // namespace descriptor

/// Map from an OpenMP enumeration value to its descriptor.
template <typename Enum, typename DescriptorTy>
using DescriptorMap = DenseMap<Enum, DescriptorTy>;

/// Return the descriptor for OpenMP clause \p C.
/// @param C Clause whose descriptor is requested.
/// @return Descriptor for clause \p C.
LLVM_ABI const descriptor::Clause &getDescriptor(llvm::omp::Clause C);
/// Return the descriptor for OpenMP modifier \p M.
/// @param M Modifier whose descriptor is requested.
/// @return Descriptor for modifier \p M.
LLVM_ABI const descriptor::Modifier &getDescriptor(llvm::omp::Modifier M);

/// Return the properties of clause \p C for OpenMP version \p Version.
/// @param C Clause whose properties are requested.
/// @param Version OpenMP language version to query.
/// @return Properties of clause \p C for version \p Version.
LLVM_ABI Properties getProperties(Clause C, unsigned Version);
} // namespace llvm::omp

#endif // LLVM_FRONTEND_OPENMP_OMPDESCRIPTORS_H
