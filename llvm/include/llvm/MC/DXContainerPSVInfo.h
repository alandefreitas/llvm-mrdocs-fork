//===- llvm/MC/DXContainerPSVInfo.h - DXContainer PSVInfo -*- C++ -------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_DXCONTAINERPSVINFO_H
#define LLVM_MC_DXCONTAINERPSVINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/TargetParser/Triple.h"

#include <array>
#include <stdint.h>

namespace llvm {

class raw_ostream;

namespace mcdxbc {

/// In-memory representation of one PSV0 signature element.
struct PSVSignatureElement {
  /// Semantic name for this element.
  StringRef Name;
  /// Semantic indices for each row occupied by this element.
  SmallVector<uint32_t> Indices;
  /// Starting row in the signature.
  uint8_t StartRow;
  /// Number of columns (components) occupied.
  uint8_t Cols;
  /// Starting column in the row.
  uint8_t StartCol;
  /// Whether this element is allocated to a register.
  bool Allocated;
  /// Semantic kind.
  dxbc::PSV::SemanticKind Kind;
  /// Component type.
  dxbc::PSV::ComponentType Type;
  /// Interpolation mode.
  dxbc::PSV::InterpolationMode Mode;
  /// Dynamically indexed component mask.
  uint8_t DynamicMask;
  /// Stream index.
  uint8_t Stream;
};

/// Helper for reading and writing PSV RuntimeInfo data.
///
/// It is implemented in the BinaryFormat library so that it can be used by both
/// the MC layer and Object tools. This structure is used to represent the
/// extracted data in an inspectable and modifiable format, and can be used to
/// serialize the data back into valid PSV RuntimeInfo.
struct PSVRuntimeInfo {
  /// Construct an empty runtime-info helper with a zeroed base record.
  PSVRuntimeInfo() : DXConStrTabBuilder(StringTableBuilder::DXContainer) {
    memset((void *)&BaseData, 0, sizeof(dxbc::PSV::v3::RuntimeInfo));
  }
  /// True once \c finalize() has prepared internal tables for serialization.
  bool IsFinalized = false;
  /// Version-3 runtime-info header fields used as the serialization base.
  dxbc::PSV::v3::RuntimeInfo BaseData;
  /// Resource bind infos associated with this shader.
  SmallVector<dxbc::PSV::v2::ResourceBindInfo> Resources;
  /// Input signature elements.
  SmallVector<PSVSignatureElement> InputElements;
  /// Output signature elements.
  SmallVector<PSVSignatureElement> OutputElements;
  /// Patch-constant or primitive signature elements.
  SmallVector<PSVSignatureElement> PatchOrPrimElements;

  // TODO: Make this interface user-friendly.
  // The interface here is bad, and we'll want to change this in the future. We
  // probably will want to build out these mask vectors as vectors of bools and
  // have this utility object convert them to the bit masks. I don't want to
  // over-engineer this API now since we don't know what the data coming in to
  // feed it will look like, so I kept it extremely simple for the immediate use
  // case.
  /// Per-stream output ViewID dependency masks as packed 32-bit words.
  std::array<SmallVector<uint32_t>, 4> OutputVectorMasks;
  /// Patch-constant or primitive ViewID dependency masks as packed 32-bit
  /// words.
  SmallVector<uint32_t> PatchOrPrimMasks;
  /// Per-stream input-to-output dependency maps as packed 32-bit words.
  std::array<SmallVector<uint32_t>, 4> InputOutputMap;
  /// Input-to-patch-constant dependency map as packed 32-bit words.
  SmallVector<uint32_t> InputPatchMap;
  /// Patch-constant-to-output dependency map as packed 32-bit words.
  SmallVector<uint32_t> PatchOutputMap;
  /// Shader entry-point name stored in the PSV string table.
  StringRef EntryName;

  /// Serialize PSVInfo into the provided raw_ostream.
  ///
  /// The version field specifies the data version to encode; the default value
  /// specifies encoding the highest supported version.
  ///
  /// \param OS - Stream that receives the serialized PSV RuntimeInfo.
  /// \param Version - PSV data version to encode, or the maximum supported
  ///        version when left at the default.
  LLVM_ABI void
  write(raw_ostream &OS,
        uint32_t Version = std::numeric_limits<uint32_t>::max()) const;

  /// Finalize internal tables so that \c write() can serialize this info.
  ///
  /// \param Stage - Shader stage that selects stage-specific runtime fields.
  /// \param Version - PSV data version to prepare for, or the maximum supported
  ///        version when left at the default.
  LLVM_ABI void
  finalize(Triple::EnvironmentType Stage,
           uint32_t Version = std::numeric_limits<uint32_t>::max());

private:
  SmallVector<uint32_t, 64> IndexBuffer;
  SmallVector<llvm::dxbc::PSV::v0::SignatureElement, 32> SignatureElements;
  StringTableBuilder DXConStrTabBuilder;
};

/// Helper for building and serializing a DXContainer program signature.
class Signature {
  struct Parameter {
    uint32_t Stream;
    StringRef Name;
    uint32_t Index;
    dxbc::D3DSystemValue SystemValue;
    dxbc::SigComponentType CompType;
    uint32_t Register;
    uint8_t Mask;
    uint8_t ExclusiveMask;
    dxbc::SigMinPrecision MinPrecision;
  };

  SmallVector<Parameter> Params;

public:
  /// Append a signature parameter with the given fields.
  ///
  /// \param Stream - Stream index for this parameter.
  /// \param Name - Semantic name.
  /// \param Index - Semantic index.
  /// \param SystemValue - System-value semantic kind.
  /// \param CompType - Component type of the parameter bits.
  /// \param Register - Register (row) index.
  /// \param Mask - Component mask describing column allocation.
  /// \param ExclusiveMask - Components never written (output) or always read
  ///        (input).
  /// \param MinPrecision - Minimum precision of the components.
  void addParam(uint32_t Stream, StringRef Name, uint32_t Index,
                dxbc::D3DSystemValue SystemValue,
                dxbc::SigComponentType CompType, uint32_t Register,
                uint8_t Mask, uint8_t ExclusiveMask,
                dxbc::SigMinPrecision MinPrecision) {
    Params.push_back(Parameter{Stream, Name, Index, SystemValue, CompType,
                               Register, Mask, ExclusiveMask, MinPrecision});
  }

  /// Serialize the signature parameters to \p OS.
  ///
  /// \param OS - Stream that receives the serialized signature.
  LLVM_ABI void write(raw_ostream &OS);
};

} // namespace mcdxbc
} // namespace llvm

#endif // LLVM_MC_DXCONTAINERPSVINFO_H
