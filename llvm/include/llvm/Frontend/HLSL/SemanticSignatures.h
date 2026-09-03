//===- SemanticSignatures.h - HLSL Semantic Signature helper objects ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains structure definitions of HLSL Semantic Signature
/// objects.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_HLSL_SEMANTICSIGNATURES_H
#define LLVM_FRONTEND_HLSL_SEMANTICSIGNATURES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DXILABI.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

class LLVMContext;
class MDNode;

namespace hlsl {

// Definitions of the in-memory data layout structures

// Sentinel values denoting that an element is unallocated
static constexpr uint32_t UnallocatedRow = ~0U;
static constexpr uint8_t UnallocatedCol = 0xFF;

/// Models a single packed range of signature rows with its semantic name and
/// indices, register placement, component masks, and stage-specific attributes.
struct SemanticSignatureElement {
  /// Unique identifier for this signature element within its signature.
  uint32_t SigId;
  /// Semantic name string for this signature element (e.g. \c TEXCOORD).
  StringRef SemanticName;
  /// Component element type of the packed signature data.
  dxil::ElementType CompType;
  /// Kind of system or user semantic associated with this element.
  dxbc::PSV::SemanticKind SemanticKind;
  /// Per-row semantic indices for the packed range of signature rows.
  SmallVector<uint32_t> SemanticIndices;
  /// Interpolation mode applied to this signature element.
  dxbc::PSV::InterpolationMode InterpMode =
      dxbc::PSV::InterpolationMode::Undefined;
  /// Number of consecutive signature rows occupied by this element.
  uint32_t Rows;
  /// Number of consecutive components (columns) occupied by this element.
  uint8_t Cols;
  /// Starting register row, or \c UnallocatedRow if not yet allocated.
  uint32_t StartRow = UnallocatedRow;
  /// Starting component column, or \c UnallocatedCol if not yet allocated.
  uint8_t StartCol = UnallocatedCol;
  /// Bitmask of components that are used (read or written) by this element.
  uint8_t UsageMask = 0;
  /// Bitmask of components that may be dynamically indexed.
  uint8_t DynIndexMask = 0;
  /// Geometry-shader output stream index for this element, if applicable.
  uint32_t GSStream = 0;

  /// Construct an empty, unallocated signature element.
  SemanticSignatureElement() = default;
  /// Construct a signature element with the given identity and dimensions.
  ///
  /// \param SigId Unique identifier for this signature element.
  /// \param SemanticName Semantic name string for this element.
  /// \param CompType Component element type of the packed data.
  /// \param SemanticKind Kind of system or user semantic.
  /// \param SemanticIndices Per-row semantic indices; also sets \c Rows.
  /// \param Cols Number of consecutive components occupied by this element.
  SemanticSignatureElement(uint32_t SigId, StringRef SemanticName,
                           dxil::ElementType CompType,
                           dxbc::PSV::SemanticKind SemanticKind,
                           ArrayRef<uint32_t> SemanticIndices, uint8_t Cols)
      : SigId(SigId), SemanticName(SemanticName), CompType(CompType),
        SemanticKind(SemanticKind), SemanticIndices(SemanticIndices),
        Rows(static_cast<uint32_t>(SemanticIndices.size())), Cols(Cols) {}

  /// Return true if this element has been assigned a register location.
  ///
  /// \return \c true if a register location has been assigned.
  bool isAllocated() const {
    return StartRow != UnallocatedRow && StartCol != UnallocatedCol;
  }

  /// Return the bitmask of components declared by this element's allocation.
  ///
  /// \return Bitmask of components declared by this element's allocation, or
  ///         zero if unallocated.
  uint8_t getDeclaredMask() const {
    if (!isAllocated())
      return 0;
    return static_cast<uint8_t>(((1U << Cols) - 1U) << StartCol);
  }

  /// Return the bitmask of components that are always read from this element.
  ///
  /// \return Bitmask of components that are always read from this element.
  uint8_t getAlwaysReadsMask() const { return UsageMask; }

  /// Return the bitmask of declared components that are never written.
  ///
  /// \return Bitmask of declared components that are never written.
  uint8_t getNeverWritesMask() const {
    return static_cast<uint8_t>(~UsageMask & getDeclaredMask());
  }

  /// Return the DXBC minimum-precision encoding for this element's type.
  ///
  /// \param UseMinPrecision Whether min-precision encodings should be used.
  /// \return The DXBC minimum-precision encoding for this element's type.
  dxbc::SigMinPrecision getMinPrecision(bool UseMinPrecision) const {
    if (!UseMinPrecision)
      return dxbc::SigMinPrecision::Default;
    switch (CompType) {
    case dxil::ElementType::F16:
      return dxbc::SigMinPrecision::Float16;
    case dxil::ElementType::I16:
      return dxbc::SigMinPrecision::SInt16;
    case dxil::ElementType::U16:
      return dxbc::SigMinPrecision::UInt16;
    default:
      return dxbc::SigMinPrecision::Default;
    }
  }

  /// Parse a signature element from its metadata representation.
  ///
  /// \param Node Metadata node describing a signature element.
  /// \return The parsed element, or an error if \p Node is malformed.
  LLVM_ABI static Expected<SemanticSignatureElement>
  fromMetadata(const MDNode *Node);

  /// Build the metadata representation of this signature element.
  ///
  /// \param Ctx LLVM context used to create metadata nodes.
  /// \return Metadata node representing this signature element.
  LLVM_ABI MDNode *toMetadata(LLVMContext &Ctx) const;
};

/// Map a semantic name string to its corresponding \c PSV::SemanticKind.
///
/// \param SemanticName Semantic name to classify (e.g. \c SV_Position).
/// \return The matching semantic kind, or a user-defined kind if unrecognized.
LLVM_ABI dxbc::PSV::SemanticKind getSemanticKind(StringRef SemanticName);

} // namespace hlsl
} // namespace llvm

#endif // LLVM_FRONTEND_HLSL_SEMANTICSIGNATURES_H
