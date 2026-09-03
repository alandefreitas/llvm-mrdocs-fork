//===- HLSLBinding.h - Representation for resource bindings in HLSL -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains objects to represent resource bindings.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_HLSL_HLSLBINDING_H
#define LLVM_FRONTEND_HLSL_HLSLBINDING_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DXILABI.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm {
namespace hlsl {

/// Ranges of bindings and free space for each `dxil::ResourceClass`.
///
/// This can represent HLSL-level bindings as well as bindings described in
/// root signatures, and can be used for analysis of overlapping or missing
/// bindings as well as for finding space for implicit bindings.
///
/// As an example, given these resource bindings:
///
/// RWBuffer<float> A[10] : register(u3);
/// RWBuffer<float> B[] : register(u5, space2)
///
/// The binding info for UAV bindings should look like this:
///
/// UAVSpaces {
///   ResClass = ResourceClass::UAV,
///   Spaces = {
///     { Space = 0u, FreeRanges = {{ 0u, 2u }, { 13u, ~0u }} },
///     { Space = 2u, FreeRanges = {{ 0u, 4u }} }
///   }
/// }
class BindingInfo {
public:
  /// Inclusive range of consecutive register bindings.
  struct BindingRange {
    /// Lowest register index in the range.
    uint32_t LowerBound;
    /// Highest register index in the range.
    uint32_t UpperBound;
    /// Construct a binding range from lower and upper bounds.
    /// \param LB Lowest register index in the range.
    /// \param UB Highest register index in the range.
    BindingRange(uint32_t LB, uint32_t UB) : LowerBound(LB), UpperBound(UB) {}
  };

  /// Free binding ranges within a single register space.
  struct RegisterSpace {
    /// Register space number.
    uint32_t Space;
    /// Contiguous free ranges remaining in this space.
    SmallVector<BindingRange> FreeRanges;
    /// Construct a register space with all registers initially free.
    /// \param Space Register space number.
    RegisterSpace(uint32_t Space) : Space(Space) {
      FreeRanges.emplace_back(0, ~0u);
    }
    /// Find the lowest free binding large enough for \p Size registers.
    ///
    /// \param Size Number of registers needed, or \c -1 for an unbounded array.
    /// \return Starting register index, or \c std::nullopt if none fits.
    LLVM_ABI std::optional<uint32_t> findAvailableBinding(int32_t Size);
  };

  /// Register spaces available for a single resource class.
  struct BindingSpaces {
    /// Resource class these spaces belong to.
    dxil::ResourceClass RC;
    /// Register spaces for this resource class.
    llvm::SmallVector<RegisterSpace> Spaces;
    /// Construct binding spaces for the given resource class.
    /// \param RC Resource class these spaces track.
    BindingSpaces(dxil::ResourceClass RC) : RC(RC) {}
    /// Return the register space for \p Space, inserting an empty one if needed.
    /// \param Space Register space number to look up or create.
    /// \return Reference to the register space for \p Space.
    LLVM_ABI RegisterSpace &getOrInsertSpace(uint32_t Space);
  };

private:
  BindingSpaces SRVSpaces{dxil::ResourceClass::SRV};
  BindingSpaces UAVSpaces{dxil::ResourceClass::UAV};
  BindingSpaces CBufferSpaces{dxil::ResourceClass::CBuffer};
  BindingSpaces SamplerSpaces{dxil::ResourceClass::Sampler};

public:
  /// Return the binding spaces for the given resource class.
  /// \param RC Resource class whose spaces to retrieve.
  /// \return Binding spaces for \p RC.
  BindingSpaces &getBindingSpaces(dxil::ResourceClass RC) {
    switch (RC) {
    case dxil::ResourceClass::SRV:
      return SRVSpaces;
    case dxil::ResourceClass::UAV:
      return UAVSpaces;
    case dxil::ResourceClass::CBuffer:
      return CBufferSpaces;
    case dxil::ResourceClass::Sampler:
      return SamplerSpaces;
    }

    llvm_unreachable("Invalid resource class");
  }
  /// Return the binding spaces for the given resource class.
  /// \param RC Resource class whose spaces to retrieve.
  /// \return Binding spaces for \p RC.
  const BindingSpaces &getBindingSpaces(dxil::ResourceClass RC) const {
    return const_cast<BindingInfo *>(this)->getBindingSpaces(RC);
  }

  /// Find an available binding in \p Space for \p Size registers of class \p RC.
  ///
  /// \param RC Resource class to allocate in.
  /// \param Space Register space number to search.
  /// \param Size Number of registers needed, or \c -1 for an unbounded array.
  /// \return Starting register index, or \c std::nullopt if none fits.
  LLVM_ABI std::optional<uint32_t>
  findAvailableBinding(dxil::ResourceClass RC, uint32_t Space, int32_t Size);

  friend class BindingInfoBuilder;
};

/// A single resource binding with its resource class, space, and range.
struct Binding {
  /// Resource class of this binding.
  dxil::ResourceClass RC;
  /// Register space number.
  uint32_t Space;
  /// Lowest register index in the binding range.
  uint32_t LowerBound;
  /// Highest register index in the binding range.
  uint32_t UpperBound;
  /// Opaque cookie identifying the entity that owns this binding.
  const void *Cookie;

  /// Construct a binding for the given class, space, range, and owner.
  /// \param RC Resource class of the binding.
  /// \param Space Register space number.
  /// \param LowerBound Lowest register index in the range.
  /// \param UpperBound Highest register index in the range.
  /// \param Cookie Opaque cookie for the owning entity.
  Binding(dxil::ResourceClass RC, uint32_t Space, uint32_t LowerBound,
          uint32_t UpperBound, const void *Cookie)
      : RC(RC), Space(Space), LowerBound(LowerBound), UpperBound(UpperBound),
        Cookie(Cookie) {}

  /// Return true if this binding extends to the end of the register space.
  /// \return True if the binding is unbounded.
  bool isUnbounded() const { return UpperBound == ~0U; }

  /// Return true if this binding equals \p RHS.
  /// \param RHS Binding to compare against.
  /// \return True if the bindings are equal.
  bool operator==(const Binding &RHS) const {
    return std::tie(RC, Space, LowerBound, UpperBound, Cookie) ==
           std::tie(RHS.RC, RHS.Space, RHS.LowerBound, RHS.UpperBound,
                    RHS.Cookie);
  }
  /// Return true if this binding is not equal to \p RHS.
  /// \param RHS Binding to compare against.
  /// \return True if the bindings are not equal.
  bool operator!=(const Binding &RHS) const { return !(*this == RHS); }

  /// Order bindings by resource class, space, then lower bound.
  /// \param RHS Binding to compare against.
  /// \return True if this binding orders before \p RHS.
  bool operator<(const Binding &RHS) const {
    return std::tie(RC, Space, LowerBound) <
           std::tie(RHS.RC, RHS.Space, RHS.LowerBound);
  }
};

/// Sorted collection of resource bindings for lookup by range.
class BoundRegs {
  SmallVector<Binding> Bindings;

public:
  /// Construct from a sorted vector of bindings.
  /// \param Bindings Sorted bindings to take ownership of.
  BoundRegs(SmallVector<Binding> &&Bindings) : Bindings(std::move(Bindings)) {}

  /// Find a binding that fully covers the given register range.
  ///
  /// \param RC Resource class to match.
  /// \param Space Register space number to match.
  /// \param LowerBound Lowest register index of the query range.
  /// \param UpperBound Highest register index of the query range.
  /// \return Matching binding, or \c nullptr if none covers the range.
  const Binding *findBoundReg(dxil::ResourceClass RC, uint32_t Space,
                              uint32_t LowerBound, uint32_t UpperBound) const {
    // UpperBound and Cookie are given dummy values, since they aren't
    // interesting for operator<
    const Binding *It =
        llvm::upper_bound(Bindings, Binding{RC, Space, LowerBound, 0, nullptr});
    if (It == Bindings.begin())
      return nullptr;
    --It;
    if (It->RC == RC && It->Space == Space && It->LowerBound <= LowerBound &&
        It->UpperBound >= UpperBound)
      return It;
    return nullptr;
  }
};

/// Builder class for creating a \c BindingInfo.
class BindingInfoBuilder {
private:
  SmallVector<Binding> Bindings;

public:
  /// Record a resource binding to include in the computed binding info.
  ///
  /// \param RC Resource class of the binding.
  /// \param Space Register space number.
  /// \param LowerBound Lowest register index in the range.
  /// \param UpperBound Highest register index in the range.
  /// \param Cookie Opaque cookie for the owning entity.
  void trackBinding(dxil::ResourceClass RC, uint32_t Space, uint32_t LowerBound,
                    uint32_t UpperBound, const void *Cookie) {
    Bindings.emplace_back(RC, Space, LowerBound, UpperBound, Cookie);
  }
  /// Calculate the binding info - \c ReportOverlap will be called once for each
  /// overlapping binding.
  /// \param ReportOverlap Callback invoked once for each overlapping binding.
  /// \return Computed binding info from the tracked bindings.
  LLVM_ABI BindingInfo calculateBindingInfo(
      llvm::function_ref<void(const BindingInfoBuilder &Builder,
                              const Binding &Overlapping)>
          ReportOverlap);

  /// Calculate the binding info - \c HasOverlap will be set to indicate whether
  /// there are any overlapping bindings.
  /// \param HasOverlap Set to true if any overlapping bindings are found.
  /// \return Computed binding info from the tracked bindings.
  BindingInfo calculateBindingInfo(bool &HasOverlap) {
    HasOverlap = false;
    return calculateBindingInfo(
        [&HasOverlap](auto, auto) { HasOverlap = true; });
  }

  /// Take ownership of the sorted bindings as a \c BoundRegs collection.
  ///
  /// Must be called only after \c calculateBindingInfo, which sorts the
  /// tracked bindings.
  /// \return Sorted bindings as a \c BoundRegs collection.
  BoundRegs takeBoundRegs() {
    assert(std::is_sorted(Bindings.begin(), Bindings.end()) &&
           "takeBoundRegs should only be called after calculateBindingInfo");
    return BoundRegs(std::move(Bindings));
  }

  /// For use in the \c ReportOverlap callback of \c calculateBindingInfo -
  /// finds a binding that the \c ReportedBinding overlaps with.
  /// \param ReportedBinding Binding reported as overlapping.
  /// \return A binding that overlaps with \p ReportedBinding.
  LLVM_ABI const Binding &findOverlapping(const Binding &ReportedBinding) const;
};

} // namespace hlsl
} // namespace llvm

#endif // LLVM_FRONTEND_HLSL_HLSLBINDING_H
