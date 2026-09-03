//===-- llvm/BinaryFormat/DXContainer.h - The DXBC file format --*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines manifest constants for the DXContainer object file format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_DXCONTAINER_H
#define LLVM_BINARYFORMAT_DXCONTAINER_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DXILABI.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SwapByteOrder.h"
#include "llvm/TargetParser/Triple.h"

#include <stdint.h>

namespace llvm {
template <typename, unsigned> class EnumStrings;

// The DXContainer file format is arranged as a header and "parts". Semantically
// parts are similar to sections in other object file formats. The File format
// structure is roughly:

// ┌────────────────────────────────┐
// │             Header             │
// ├────────────────────────────────┤
// │              Part              │
// ├────────────────────────────────┤
// │              Part              │
// ├────────────────────────────────┤
// │              ...               │
// └────────────────────────────────┘

/// Constants, structures, and helpers for the DirectX DXContainer (DXBC)
/// object-file format.
namespace dxbc {

// Expanded (instead of the macro) so MrDocs can attach docs to each using.
/// Bring bitmask enum bitwise NOT into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator~;
/// Bring bitmask enum bitwise OR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator|;
/// Bring bitmask enum bitwise AND into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator&;
/// Bring bitmask enum bitwise XOR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator^;
/// Bring bitmask enum left-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator<<;
/// Bring bitmask enum right-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator>>;
/// Bring bitmask enum in-place OR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator|=;
/// Bring bitmask enum in-place AND into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator&=;
/// Bring bitmask enum in-place XOR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator^=;
/// Bring bitmask enum in-place left-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator<<=;
/// Bring bitmask enum in-place right-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator>>=;
/// Bring bitmask enum logical-not into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator!;
/// Bring bitmask enum any-bits-set test into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::any;

constexpr static uint64_t DXCONTAINER_STRUCT_ALIGNMENT = 4;

/// Map a DXBC shader-kind integer to a Triple environment type.
///
/// \param Kind Shader kind as stored in ProgramHeader::ShaderKind.
/// @return Triple environment type corresponding to \p Kind.
inline Triple::EnvironmentType getShaderStage(uint32_t Kind) {
  assert(Kind <= Triple::RootSignature - Triple::Pixel &&
         "Shader kind out of expected range.");
  return static_cast<Triple::EnvironmentType>(Triple::Pixel + Kind);
}

/// 16-byte digest used by the DXContainer file hash.
struct Hash {
  /// Digest bytes.
  uint8_t Digest[16];
};

/// Flags that describe how a shader hash was computed.
enum class HashFlags : uint32_t {
  None = 0,           ///< No flags defined.
  IncludesSource = 1, ///< Hash includes source information (\c -Zss).
};

/// Shader hash stored in a HASH part.
struct ShaderHash {
  /// Hash flags (\c HashFlags).
  uint32_t Flags;
  /// Hash digest.
  uint8_t Digest[16];

  /// Return true if this hash has a non-zero flag or digest.
  ///
  /// @return True if this hash has a non-zero flag or digest.
  LLVM_ABI bool isPopulated();

  /// Byte-swap multi-byte fields of this hash.
  void swapBytes() { sys::swapByteOrder(Flags); }
};

/// DXContainer format version.
struct ContainerVersion {
  /// Major version number.
  uint16_t Major;
  /// Minor version number.
  uint16_t Minor;

  /// Byte-swap multi-byte fields of this version.
  void swapBytes() {
    sys::swapByteOrder(Major);
    sys::swapByteOrder(Minor);
  }
};

/// File header at the start of a DXContainer.
///
/// Structure is followed by part offsets: uint32_t PartOffset[PartCount];
/// The offset is to a PartHeader, which is followed by the Part Data.
struct Header {
  /// ASCII "DXBC".
  uint8_t Magic[4];
  /// Hash of the container contents following this field.
  Hash FileHash;
  /// Container format version.
  ContainerVersion Version;
  /// Total size of the file in bytes.
  uint32_t FileSize;
  /// Number of parts in the container.
  uint32_t PartCount;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() {
    Version.swapBytes();
    sys::swapByteOrder(FileSize);
    sys::swapByteOrder(PartCount);
  }
};

/// Describes the size and type of a DXIL container part.
///
/// Structure is followed directly by part data: uint8_t PartData[PartSize].
struct PartHeader {
  /// Four-character part name.
  uint8_t Name[4];
  /// Size in bytes of the part data following this header.
  uint32_t Size;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() { sys::swapByteOrder(Size); }
  /// Return the four-character part name as a StringRef.
  ///
  /// @return Four-character part name.
  StringRef getName() const {
    return StringRef(reinterpret_cast<const char *>(&Name[0]), 4);
  }
};

/// Header of the LLVM bitcode blob inside a program part.
///
/// Followed by uint8_t[BitcodeHeader.Size] at &BitcodeHeader + Header.Offset.
struct BitcodeHeader {
  /// ASCII "DXIL".
  uint8_t Magic[4];
  /// DXIL minor version.
  uint8_t MinorVersion;
  /// DXIL major version.
  uint8_t MajorVersion;
  /// Padding.
  uint16_t Unused;
  /// Offset to LLVM bitcode (from start of header).
  uint32_t Offset;
  /// Size of LLVM bitcode (in bytes).
  uint32_t Size;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() {
    sys::swapByteOrder(MinorVersion);
    sys::swapByteOrder(MajorVersion);
    sys::swapByteOrder(Offset);
    sys::swapByteOrder(Size);
  }
};

/// Header of a program (DXIL or ILDB) part.
struct ProgramHeader {
  /// Packed shader-model version (major in the high nibble, minor in the low).
  uint8_t Version;
  /// Padding.
  uint8_t Unused;
  /// Shader kind; convert with \c getShaderStage.
  uint16_t ShaderKind;
  /// Size in uint32_t words including this header.
  uint32_t Size;
  /// Embedded DXIL bitcode header.
  BitcodeHeader Bitcode;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() {
    sys::swapByteOrder(ShaderKind);
    sys::swapByteOrder(Size);
    Bitcode.swapBytes();
  }
  /// Return the shader-model major version.
  ///
  /// @return Shader-model major version from the high nibble of Version.
  uint8_t getMajorVersion() { return Version >> 4; }
  /// Return the shader-model minor version.
  ///
  /// @return Shader-model minor version from the low nibble of Version.
  uint8_t getMinorVersion() { return Version & 0xF; }
  /// Pack major and minor shader-model versions into a Version byte.
  ///
  /// \param Major Shader-model major version.
  /// \param Minor Shader-model minor version.
  /// @return Packed Version byte with major in the high nibble and minor in
  ///         the low.
  static uint8_t getVersion(uint8_t Major, uint8_t Minor) {
    return (Major << 4) | Minor;
  }
};

static_assert(sizeof(ProgramHeader) == 24, "ProgramHeader Size incorrect!");

#define CONTAINER_PART(Part) Part,
/// Four-character DXContainer part identifiers.
enum class PartType {
  Unknown = 0, ///< Unrecognized or unspecified part.
#include "DXContainerConstants.def"
};

#define SHADER_FEATURE_FLAG(Num, DxilModuleNum, Val, Str) Val = 1ull << Num,
/// Shader feature flags stored in the SFI0 part.
enum class FeatureFlags : uint64_t {
#include "DXContainerConstants.def"
};
static_assert((uint64_t)FeatureFlags::NextUnusedBit <= 1ull << 63,
              "Shader flag bits exceed enum size.");

#define ROOT_SIGNATURE_FLAG(Num, Val) Val = Num,
/// D3D12 root signature flags.
enum class RootFlags : uint32_t {
#include "DXContainerConstants.def"

  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(SamplerHeapDirectlyIndexed)
};

/// Return the enumerator name table for \c RootFlags.
///
/// @return Enumerator name table for RootFlags.
LLVM_ABI EnumStrings<RootFlags, 1> getRootFlags();

#define ROOT_DESCRIPTOR_FLAG(Num, Enum, Flag) Enum = Num,
/// Flags on a root CBV, SRV, or UAV descriptor.
enum class RootDescriptorFlags : uint32_t {
#include "DXContainerConstants.def"

  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(DataStatic)
};

/// Return the enumerator name table for \c RootDescriptorFlags.
///
/// @return Enumerator name table for RootDescriptorFlags.
LLVM_ABI EnumStrings<RootDescriptorFlags, 1> getRootDescriptorFlags();

#define DESCRIPTOR_RANGE_FLAG(Num, Enum, Flag) Enum = Num,
/// Flags on a descriptor range in a descriptor table.
enum class DescriptorRangeFlags : uint32_t {
#include "DXContainerConstants.def"

  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(DescriptorsStaticKeepingBufferBoundsChecks)
};

/// Return the enumerator name table for \c DescriptorRangeFlags.
///
/// @return Enumerator name table for DescriptorRangeFlags.
LLVM_ABI EnumStrings<DescriptorRangeFlags, 1> getDescriptorRangeFlags();

#define STATIC_SAMPLER_FLAG(Num, Enum, Flag) Enum = Num,
/// Flags on a static sampler.
enum class StaticSamplerFlags : uint32_t {
#include "DXContainerConstants.def"

  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(NonNormalizedCoordinates)
};

/// Return the enumerator name table for \c StaticSamplerFlags.
///
/// @return Enumerator name table for StaticSamplerFlags.
LLVM_ABI EnumStrings<StaticSamplerFlags, 1> getStaticSamplerFlags();

#define ROOT_PARAMETER(Val, Enum) Enum = Val,
/// Kinds of parameters in a D3D12 root signature.
enum class RootParameterType : uint32_t {
#include "DXContainerConstants.def"
};

/// Return the enumerator name table for \c RootParameterType.
///
/// @return Enumerator name table for RootParameterType.
LLVM_ABI EnumStrings<RootParameterType, 1> getRootParameterTypes();

/// Return true if \p V is a valid \c RootParameterType value.
///
/// \param V Candidate root-parameter type.
/// @return True if \p V is a valid RootParameterType.
LLVM_ABI bool isValidParameterType(uint32_t V);

/// Return true if \p V is a valid descriptor-range resource class.
///
/// \param V Candidate range type (\c dxil::ResourceClass).
/// @return True if \p V is a valid descriptor-range resource class.
LLVM_ABI bool isValidRangeType(uint32_t V);

#define SHADER_VISIBILITY(Val, Enum) Enum = Val,
/// Shader stages that can access a root-signature parameter or sampler.
enum class ShaderVisibility : uint32_t {
#include "DXContainerConstants.def"
};

/// Return the enumerator name table for \c ShaderVisibility.
///
/// @return Enumerator name table for ShaderVisibility.
LLVM_ABI EnumStrings<ShaderVisibility, 1> getShaderVisibility();

/// Return true if \p V is a valid \c ShaderVisibility value.
///
/// \param V Candidate shader-visibility value.
/// @return True if \p V is a valid ShaderVisibility.
LLVM_ABI bool isValidShaderVisibility(uint32_t V);

#define FILTER(Val, Enum) Enum = Val,
/// Sampler filter modes.
enum class SamplerFilter : uint32_t {
#include "DXContainerConstants.def"
};

/// Return true if \p V is a valid \c SamplerFilter value.
///
/// \param V Candidate sampler-filter value.
/// @return True if \p V is a valid SamplerFilter.
LLVM_ABI bool isValidSamplerFilter(uint32_t V);

/// Return the enumerator name table for \c SamplerFilter.
///
/// @return Enumerator name table for SamplerFilter.
LLVM_ABI EnumStrings<SamplerFilter, 1> getSamplerFilters();

#define TEXTURE_ADDRESS_MODE(Val, Enum) Enum = Val,
/// Texture addressing modes for static samplers.
enum class TextureAddressMode : uint32_t {
#include "DXContainerConstants.def"
};

/// Return the enumerator name table for \c TextureAddressMode.
///
/// @return Enumerator name table for TextureAddressMode.
LLVM_ABI EnumStrings<TextureAddressMode, 1> getTextureAddressModes();

/// Return true if \p V is a valid \c TextureAddressMode value.
///
/// \param V Candidate texture-address mode.
/// @return True if \p V is a valid TextureAddressMode.
LLVM_ABI bool isValidAddress(uint32_t V);

#define COMPARISON_FUNC(Val, Enum) Enum = Val,
/// Comparison functions used by comparison sampling.
enum class ComparisonFunc : uint32_t {
#include "DXContainerConstants.def"
};

/// Return the enumerator name table for \c ComparisonFunc.
///
/// @return Enumerator name table for ComparisonFunc.
LLVM_ABI EnumStrings<ComparisonFunc, 1> getComparisonFuncs();

/// Return true if \p V is a valid \c ComparisonFunc value.
///
/// \param V Candidate comparison-function value.
/// @return True if \p V is a valid ComparisonFunc.
LLVM_ABI bool isValidComparisonFunc(uint32_t V);

#define STATIC_BORDER_COLOR(Val, Enum) Enum = Val,
/// Border colors available to static samplers.
enum class StaticBorderColor : uint32_t {
#include "DXContainerConstants.def"
};

/// Return true if \p V is a valid \c StaticBorderColor value.
///
/// \param V Candidate border-color value.
/// @return True if \p V is a valid StaticBorderColor.
LLVM_ABI bool isValidBorderColor(uint32_t V);

/// Return true if \p V is a valid combination of \c RootDescriptorFlags.
///
/// \param V Candidate root-descriptor flags bitfield.
/// @return True if \p V is a valid RootDescriptorFlags combination.
LLVM_ABI bool isValidRootDesciptorFlags(uint32_t V);

/// Return true if \p V is a valid combination of \c DescriptorRangeFlags.
///
/// \param V Candidate descriptor-range flags bitfield.
/// @return True if \p V is a valid DescriptorRangeFlags combination.
LLVM_ABI bool isValidDescriptorRangeFlags(uint32_t V);

/// Return true if \p V is a valid combination of \c StaticSamplerFlags.
///
/// \param V Candidate static-sampler flags bitfield.
/// @return True if \p V is a valid StaticSamplerFlags combination.
LLVM_ABI bool isValidStaticSamplerFlags(uint32_t V);

/// Return the enumerator name table for \c StaticBorderColor.
///
/// @return Enumerator name table for StaticBorderColor.
LLVM_ABI EnumStrings<StaticBorderColor, 1> getStaticBorderColors();

/// Parse a four-character part name into a \c PartType.
///
/// \param S Four-character part name.
/// @return Parsed PartType, or PartType::Unknown if unrecognized.
LLVM_ABI PartType parsePartType(StringRef S);

/// Return true if \p PT is the debug program (ILDB) part.
///
/// \param PT Part type to test.
/// @return True if \p PT is the debug program (ILDB) part.
LLVM_ABI bool isDebugProgramPart(PartType PT);

/// Return the four-character part name for a program part.
///
/// \param IsDebug If true, return the debug program part name (ILDB);
///        otherwise DXIL.
/// @return Four-character program part name (DXIL or ILDB).
LLVM_ABI const char *getProgramPartName(bool IsDebug);
/// Return true if \p PartName is a program part name (DXIL or ILDB).
///
/// \param PartName Four-character part name.
/// @return True if \p PartName is DXIL or ILDB.
LLVM_ABI bool isProgramPart(StringRef PartName);

/// Vertex-shader extras stored in PipelinePSVInfo.
struct VertexPSVInfo {
  /// Whether an output position is present.
  uint8_t OutputPositionPresent;
  /// Padding.
  uint8_t Unused[3];

  /// Byte-swap multi-byte fields of this record.
  void swapBytes() {
    // nothing to swap
  }
};

/// Hull-shader extras stored in PipelinePSVInfo.
struct HullPSVInfo {
  /// Number of input control points.
  uint32_t InputControlPointCount;
  /// Number of output control points.
  uint32_t OutputControlPointCount;
  /// Tessellator domain.
  uint32_t TessellatorDomain;
  /// Tessellator output primitive.
  uint32_t TessellatorOutputPrimitive;

  /// Byte-swap multi-byte fields of this record.
  void swapBytes() {
    sys::swapByteOrder(InputControlPointCount);
    sys::swapByteOrder(OutputControlPointCount);
    sys::swapByteOrder(TessellatorDomain);
    sys::swapByteOrder(TessellatorOutputPrimitive);
  }
};

/// Domain-shader extras stored in PipelinePSVInfo.
struct DomainPSVInfo {
  /// Number of input control points.
  uint32_t InputControlPointCount;
  /// Whether an output position is present.
  uint8_t OutputPositionPresent;
  /// Padding.
  uint8_t Unused[3];
  /// Tessellator domain.
  uint32_t TessellatorDomain;

  /// Byte-swap multi-byte fields of this record.
  void swapBytes() {
    sys::swapByteOrder(InputControlPointCount);
    sys::swapByteOrder(TessellatorDomain);
  }
};

/// Geometry-shader extras stored in PipelinePSVInfo.
struct GeometryPSVInfo {
  /// Input primitive topology.
  uint32_t InputPrimitive;
  /// Output topology.
  uint32_t OutputTopology;
  /// Mask of enabled output streams.
  uint32_t OutputStreamMask;
  /// Whether an output position is present.
  uint8_t OutputPositionPresent;
  /// Padding.
  uint8_t Unused[3];

  /// Byte-swap multi-byte fields of this record.
  void swapBytes() {
    sys::swapByteOrder(InputPrimitive);
    sys::swapByteOrder(OutputTopology);
    sys::swapByteOrder(OutputStreamMask);
  }
};

/// Pixel-shader extras stored in PipelinePSVInfo.
struct PixelPSVInfo {
  /// Whether the shader writes depth.
  uint8_t DepthOutput;
  /// Whether the shader runs at sample frequency.
  uint8_t SampleFrequency;
  /// Padding.
  uint8_t Unused[2];

  /// Byte-swap multi-byte fields of this record.
  void swapBytes() {
    // nothing to swap
  }
};

/// Mesh-shader extras stored in PipelinePSVInfo.
struct MeshPSVInfo {
  /// Group-shared memory used in bytes.
  uint32_t GroupSharedBytesUsed;
  /// Group-shared memory that depends on ViewID, in bytes.
  uint32_t GroupSharedBytesDependentOnViewID;
  /// Payload size in bytes from the amplification shader.
  uint32_t PayloadSizeInBytes;
  /// Maximum number of output vertices.
  uint16_t MaxOutputVertices;
  /// Maximum number of output primitives.
  uint16_t MaxOutputPrimitives;

  /// Byte-swap multi-byte fields of this record.
  void swapBytes() {
    sys::swapByteOrder(GroupSharedBytesUsed);
    sys::swapByteOrder(GroupSharedBytesDependentOnViewID);
    sys::swapByteOrder(PayloadSizeInBytes);
    sys::swapByteOrder(MaxOutputVertices);
    sys::swapByteOrder(MaxOutputPrimitives);
  }
};

/// Amplification-shader extras stored in PipelinePSVInfo.
struct AmplificationPSVInfo {
  /// Payload size in bytes passed to the mesh shader.
  uint32_t PayloadSizeInBytes;

  /// Byte-swap multi-byte fields of this record.
  void swapBytes() { sys::swapByteOrder(PayloadSizeInBytes); }
};

/// Pipeline-stage-specific PSV info, selected by shader stage.
union PipelinePSVInfo {
  /// Vertex shader PSV info.
  VertexPSVInfo VS;
  /// Hull shader PSV info.
  HullPSVInfo HS;
  /// Domain shader PSV info.
  DomainPSVInfo DS;
  /// Geometry shader PSV info.
  GeometryPSVInfo GS;
  /// Pixel shader PSV info.
  PixelPSVInfo PS;
  /// Mesh shader PSV info.
  MeshPSVInfo MS;
  /// Amplification shader PSV info.
  AmplificationPSVInfo AS;

  /// Byte-swap the live stage member for shader stage \p Stage.
  ///
  /// \param Stage Shader stage that selects which union member is live.
  void swapBytes(Triple::EnvironmentType Stage) {
    switch (Stage) {
    case Triple::EnvironmentType::Pixel:
      PS.swapBytes();
      break;
    case Triple::EnvironmentType::Vertex:
      VS.swapBytes();
      break;
    case Triple::EnvironmentType::Geometry:
      GS.swapBytes();
      break;
    case Triple::EnvironmentType::Hull:
      HS.swapBytes();
      break;
    case Triple::EnvironmentType::Domain:
      DS.swapBytes();
      break;
    case Triple::EnvironmentType::Mesh:
      MS.swapBytes();
      break;
    case Triple::EnvironmentType::Amplification:
      AS.swapBytes();
      break;
    default:
      break;
    }
  }
};

static_assert(sizeof(PipelinePSVInfo) == 4 * sizeof(uint32_t),
              "Pipeline-specific PSV info must fit in 16 bytes.");

/// Pipeline State Validation (PSV0) part layout.
namespace PSV {

#define SEMANTIC_KIND(Val, Enum) Enum = Val,
/// PSV signature semantic kinds.
enum class SemanticKind : uint8_t {
#include "DXContainerConstants.def"
};

/// Return the enumerator name table for \c SemanticKind.
///
/// @return Enumerator name table for SemanticKind.
LLVM_ABI EnumStrings<SemanticKind, 1> getSemanticKinds();

#define COMPONENT_TYPE(Val, Enum) Enum = Val,
/// PSV signature component types.
enum class ComponentType : uint8_t {
#include "DXContainerConstants.def"
};

/// Return the enumerator name table for \c ComponentType.
///
/// @return Enumerator name table for ComponentType.
LLVM_ABI EnumStrings<ComponentType, 1> getComponentTypes();

#define INTERPOLATION_MODE(Val, Enum) Enum = Val,
/// Interpolation modes for PSV signature elements.
enum class InterpolationMode : uint8_t {
#include "DXContainerConstants.def"
};

/// Return the enumerator name table for \c InterpolationMode.
///
/// @return Enumerator name table for InterpolationMode.
LLVM_ABI EnumStrings<InterpolationMode, 1> getInterpolationModes();

#define RESOURCE_TYPE(Val, Enum) Enum = Val,
/// PSV resource types.
enum class ResourceType : uint32_t {
#include "DXContainerConstants.def"
};

/// Return the enumerator name table for \c ResourceType.
///
/// @return Enumerator name table for ResourceType.
LLVM_ABI EnumStrings<ResourceType, 1> getResourceTypes();

#define RESOURCE_KIND(Val, Enum) Enum = Val,
/// PSV resource kinds.
enum class ResourceKind : uint32_t {
#include "DXContainerConstants.def"
};

/// Return the enumerator name table for \c ResourceKind.
///
/// @return Enumerator name table for ResourceKind.
LLVM_ABI EnumStrings<ResourceKind, 1> getResourceKinds();

#define RESOURCE_FLAG(Index, Enum) bool Enum = false;
/// Bitfield of PSV resource flags.
struct ResourceFlags {
  /// Construct resource flags with all bits cleared.
  ResourceFlags() : Flags(0U) {};
  /// Named bits that overlay the Flags integer.
  struct FlagsBits {
#include "llvm/BinaryFormat/DXContainerConstants.def"
  };
  union {
    /// Resource flags as a packed integer.
    uint32_t Flags;
    /// Resource flags as named bits.
    FlagsBits Bits;
  };
  /// Return true if these flags equal \p RFlags.
  ///
  /// \param RFlags Resource flags bitfield to compare against.
  /// @return True if these flags equal \p RFlags.
  bool operator==(const uint32_t RFlags) const { return Flags == RFlags; }
};

/// PSV0 version 0 records.
namespace v0 {
/// Version 0 pipeline-state validation runtime info.
struct RuntimeInfo {
  /// Pipeline-stage-specific PSV info.
  PipelinePSVInfo StageInfo;
  /// Minimum lane count required, or 0 if unused.
  uint32_t MinimumWaveLaneCount;
  /// Maximum lane count required, or 0xffffffff if unused.
  uint32_t MaximumWaveLaneCount;
  /// Byte-swap the wave-lane counts, leaving the stage-info union untouched.
  void swapBytes() {
    // Skip the union because we don't know which field it has
    sys::swapByteOrder(MinimumWaveLaneCount);
    sys::swapByteOrder(MaximumWaveLaneCount);
  }

  /// Byte-swap the pipeline-stage info for shader stage \p Stage.
  ///
  /// \param Stage Shader stage that selects which union member is live.
  void swapBytes(Triple::EnvironmentType Stage) { StageInfo.swapBytes(Stage); }
};

/// Version 0 resource bind range.
struct ResourceBindInfo {
  /// Resource type (\c ResourceType).
  ResourceType Type;
  /// Register space.
  uint32_t Space;
  /// Inclusive lower bound of the bind range.
  uint32_t LowerBound;
  /// Inclusive upper bound of the bind range.
  uint32_t UpperBound;

  /// Byte-swap multi-byte fields of this bind info.
  void swapBytes() {
    sys::swapByteOrder(Type);
    sys::swapByteOrder(Space);
    sys::swapByteOrder(LowerBound);
    sys::swapByteOrder(UpperBound);
  }
};

/// One element of a PSV0 signature.
struct SignatureElement {
  /// Offset of the semantic name in the string table.
  uint32_t NameOffset;
  /// Offset of the semantic-index array.
  uint32_t IndicesOffset;

  /// Number of rows occupied by this element.
  uint8_t Rows;
  /// Starting row in the signature.
  uint8_t StartRow;
  /// Number of columns (components) occupied.
  uint8_t Cols : 4;
  /// Starting column in the row.
  uint8_t StartCol : 2;
  /// Whether this element is allocated to a register.
  uint8_t Allocated : 1;
  /// Padding bit.
  uint8_t Unused : 1;
  /// Semantic kind.
  SemanticKind Kind;

  /// Component type.
  ComponentType Type;
  /// Interpolation mode.
  InterpolationMode Mode;
  /// Dynamically indexed component mask.
  uint8_t DynamicMask : 4;
  /// Stream index.
  uint8_t Stream : 2;
  /// Padding bits.
  uint8_t Unused2 : 2;
  /// Reserved; must be zero.
  uint8_t Reserved;

  /// Byte-swap multi-byte fields of this element.
  void swapBytes() {
    sys::swapByteOrder(NameOffset);
    sys::swapByteOrder(IndicesOffset);
  }
};

static_assert(sizeof(SignatureElement) == 4 * sizeof(uint32_t),
              "PSV Signature elements must fit in 16 bytes.");

} // namespace v0

/// PSV0 version 1 records.
namespace v1 {

/// Mesh-shader extras stored in GeometryExtraInfo.
struct MeshRuntimeInfo {
  /// Primitive output vector count for mesh shaders.
  uint8_t SigPrimVectors;
  /// Mesh shader output topology.
  uint8_t MeshOutputTopology;
};

/// Extra geometry, tessellation, or mesh info packed into version-1 runtime
/// info.
union GeometryExtraInfo {
  /// Max vertex count for GS only (max 1024).
  uint16_t MaxVertexCount;
  /// Packed vector count: output for HS, input for DS, primitive output for MS
  /// (overlaps MeshInfo::SigPrimVectors).
  uint8_t SigPatchConstOrPrimVectors;
  /// Mesh-shader runtime extras.
  MeshRuntimeInfo MeshInfo;
};
/// Version 1 runtime info, adding stage, ViewID, and signature counts.
struct RuntimeInfo : public v0::RuntimeInfo {
  /// Shader stage (PSVShaderKind).
  uint8_t ShaderStage;
  /// Whether the shader uses ViewID.
  uint8_t UsesViewID;
  /// Geometry, hull/domain, or mesh extra info.
  GeometryExtraInfo GeomData;

  /// Number of input signature elements.
  uint8_t SigInputElements;
  /// Number of output signature elements.
  uint8_t SigOutputElements;
  /// Number of patch-constant or primitive signature elements.
  uint8_t SigPatchOrPrimElements;

  /// Number of packed input vectors.
  uint8_t SigInputVectors;
  /// Number of packed output vectors per stream.
  uint8_t SigOutputVectors[4];

  /// Byte-swap multi-byte fields of this record.
  void swapBytes() {
    // nothing to swap since everything is single-byte or a union field
  }

  /// Byte-swap stage-specific fields for shader stage \p Stage.
  ///
  /// \param Stage Shader stage that selects which inherited union member is
  ///        live.
  void swapBytes(Triple::EnvironmentType Stage) {
    v0::RuntimeInfo::swapBytes(Stage);
    if (Stage == Triple::EnvironmentType::Geometry)
      sys::swapByteOrder(GeomData.MaxVertexCount);
  }
};

} // namespace v1

/// PSV0 version 2 records.
namespace v2 {
/// Version 2 runtime info, adding compute/mesh/amplification thread-group size.
struct RuntimeInfo : public v1::RuntimeInfo {
  /// Thread-group size in the X dimension.
  uint32_t NumThreadsX;
  /// Thread-group size in the Y dimension.
  uint32_t NumThreadsY;
  /// Thread-group size in the Z dimension.
  uint32_t NumThreadsZ;

  /// Byte-swap multi-byte fields of this record.
  void swapBytes() {
    sys::swapByteOrder(NumThreadsX);
    sys::swapByteOrder(NumThreadsY);
    sys::swapByteOrder(NumThreadsZ);
  }

  /// Byte-swap stage-specific fields for shader stage \p Stage.
  ///
  /// \param Stage Shader stage that selects which inherited union member is
  ///        live.
  void swapBytes(Triple::EnvironmentType Stage) {
    v1::RuntimeInfo::swapBytes(Stage);
  }
};

/// Version 2 resource bind info, adding kind and flags to the v0 layout.
struct ResourceBindInfo : public v0::ResourceBindInfo {
  /// Resource kind (\c ResourceKind).
  ResourceKind Kind;
  /// Resource flags.
  ResourceFlags Flags;

  /// Byte-swap multi-byte fields of this bind info.
  void swapBytes() {
    v0::ResourceBindInfo::swapBytes();
    sys::swapByteOrder(Kind);
    sys::swapByteOrder(Flags.Flags);
  }
};

} // namespace v2

/// PSV0 version 3 records.
namespace v3 {
/// Version 3 runtime info, adding an entry-name string-table offset.
struct RuntimeInfo : public v2::RuntimeInfo {
  /// Offset into the string table, which is stored separately in the PSV0 part.
  /// The entry name string itself is not stored in the RuntimeInfo record.
  uint32_t EntryNameOffset;

  /// Byte-swap multi-byte fields of this record.
  void swapBytes() {
    v2::RuntimeInfo::swapBytes();
    sys::swapByteOrder(EntryNameOffset);
  }

  /// Byte-swap stage-specific fields for shader stage \p Stage.
  ///
  /// \param Stage Shader stage that selects which inherited union member is
  ///        live.
  void swapBytes(Triple::EnvironmentType Stage) {
    v2::RuntimeInfo::swapBytes(Stage);
  }
};

} // namespace v3
} // namespace PSV

#define COMPONENT_PRECISION(Val, Enum) Enum = Val,
/// Minimum precision of a program-signature component.
enum class SigMinPrecision : uint32_t {
#include "DXContainerConstants.def"
};

/// Return the enumerator name table for \c SigMinPrecision.
///
/// @return Enumerator name table for SigMinPrecision.
LLVM_ABI EnumStrings<SigMinPrecision, 1> getSigMinPrecisions();

#define D3D_SYSTEM_VALUE(Val, Enum) Enum = Val,
/// D3D system-value semantics used in program signatures.
enum class D3DSystemValue : uint32_t {
#include "DXContainerConstants.def"
};

/// Return the enumerator name table for \c D3DSystemValue.
///
/// @return Enumerator name table for D3DSystemValue.
LLVM_ABI EnumStrings<D3DSystemValue, 1> getD3DSystemValues();

#define COMPONENT_TYPE(Val, Enum) Enum = Val,
/// Component type of a program-signature parameter.
enum class SigComponentType : uint32_t {
#include "DXContainerConstants.def"
};

/// Return the enumerator name table for \c SigComponentType.
///
/// @return Enumerator name table for SigComponentType.
LLVM_ABI EnumStrings<SigComponentType, 1> getSigComponentTypes();

/// Header of a program signature (ISG1/OSG1/PSG1) part.
struct ProgramSignatureHeader {
  /// Number of signature parameters.
  uint32_t ParamCount;
  /// Offset from this header to the first ProgramSignatureElement.
  uint32_t FirstParamOffset;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() {
    sys::swapByteOrder(ParamCount);
    sys::swapByteOrder(FirstParamOffset);
  }
};

/// One parameter in a program input, output, or patch-constant signature.
struct ProgramSignatureElement {
  /// Stream index (parameters must appear in non-decreasing stream order).
  uint32_t Stream;
  /// Offset from the start of the ProgramSignatureHeader to the start of the
  /// null terminated string for the name.
  uint32_t NameOffset;
  /// Semantic index.
  uint32_t Index;
  /// Semantic type. Similar to PSV::SemanticKind.
  D3DSystemValue SystemValue;
  /// Type of bits.
  SigComponentType CompType;
  /// Register index (row index).
  uint32_t Register;
  /// Mask (column allocation).
  uint8_t Mask;

  /// Components of the register that are never written (output) or always
  /// read (input).
  ///
  /// The ExclusiveMask has a different meaning for input and output
  /// signatures. For an output signature, masked components of the output
  /// register are never written to. For an input signature, masked components
  /// of the input register are always read.
  uint8_t ExclusiveMask;

  /// Padding; unused.
  uint16_t Unused;
  /// Minimum precision of input/output data.
  SigMinPrecision MinPrecision;

  /// Byte-swap multi-byte fields of this element.
  void swapBytes() {
    sys::swapByteOrder(Stream);
    sys::swapByteOrder(NameOffset);
    sys::swapByteOrder(Index);
    sys::swapByteOrder(SystemValue);
    sys::swapByteOrder(CompType);
    sys::swapByteOrder(Register);
    sys::swapByteOrder(Mask);
    sys::swapByteOrder(ExclusiveMask);
    sys::swapByteOrder(MinPrecision);
  }
};

static_assert(sizeof(ProgramSignatureElement) == 32,
              "ProgramSignatureElement is misaligned");

/// Root signature (RTS0) part layout, versioned to match D3D12.
namespace RTS0 {
/// Version 1.0 root signature records.
namespace v1 {
/// Static sampler descriptor in a version-1 root signature.
struct StaticSampler {
  /// Filter used when sampling (\c SamplerFilter).
  uint32_t Filter;
  /// Addressing mode for the U coordinate (\c TextureAddressMode).
  uint32_t AddressU;
  /// Addressing mode for the V coordinate (\c TextureAddressMode).
  uint32_t AddressV;
  /// Addressing mode for the W coordinate (\c TextureAddressMode).
  uint32_t AddressW;
  /// Offset from the calculated mipmap level.
  float MipLODBias;
  /// Clamping value for anisotropic filtering.
  uint32_t MaxAnisotropy;
  /// Comparison function for comparison sampling (\c ComparisonFunc).
  uint32_t ComparisonFunc;
  /// Border color for samples outside [0, 1] (\c StaticBorderColor).
  uint32_t BorderColor;
  /// Lower end of the mipmap range to clamp access to.
  float MinLOD;
  /// Upper end of the mipmap range to clamp access to.
  float MaxLOD;
  /// Shader register the sampler is bound to.
  uint32_t ShaderRegister;
  /// Register space of the sampler.
  uint32_t RegisterSpace;
  /// Shader stages that can access this sampler (\c ShaderVisibility).
  uint32_t ShaderVisibility;
  /// Byte-swap multi-byte fields of this sampler.
  void swapBytes() {
    sys::swapByteOrder(Filter);
    sys::swapByteOrder(AddressU);
    sys::swapByteOrder(AddressV);
    sys::swapByteOrder(AddressW);
    sys::swapByteOrder(MipLODBias);
    sys::swapByteOrder(MaxAnisotropy);
    sys::swapByteOrder(ComparisonFunc);
    sys::swapByteOrder(BorderColor);
    sys::swapByteOrder(MinLOD);
    sys::swapByteOrder(MaxLOD);
    sys::swapByteOrder(ShaderRegister);
    sys::swapByteOrder(RegisterSpace);
    sys::swapByteOrder(ShaderVisibility);
  };
};

/// Descriptor range within a version-1 descriptor table.
struct DescriptorRange {
  /// Descriptor range type (\c dxil::ResourceClass).
  uint32_t RangeType;
  /// Number of descriptors in the range, or -1 for unbounded.
  uint32_t NumDescriptors;
  /// First shader register in the range.
  uint32_t BaseShaderRegister;
  /// Register space of the range.
  uint32_t RegisterSpace;
  /// Offset of this range from the start of the descriptor table.
  uint32_t OffsetInDescriptorsFromTableStart;
  /// Byte-swap multi-byte fields of this range.
  void swapBytes() {
    sys::swapByteOrder(RangeType);
    sys::swapByteOrder(NumDescriptors);
    sys::swapByteOrder(BaseShaderRegister);
    sys::swapByteOrder(RegisterSpace);
    sys::swapByteOrder(OffsetInDescriptorsFromTableStart);
  }
};

/// Root CBV, SRV, or UAV descriptor in a version-1 root signature.
struct RootDescriptor {
  /// Shader register the descriptor is bound to.
  uint32_t ShaderRegister;
  /// Register space of the descriptor.
  uint32_t RegisterSpace;
  /// Byte-swap multi-byte fields of this descriptor.
  void swapBytes() {
    sys::swapByteOrder(ShaderRegister);
    sys::swapByteOrder(RegisterSpace);
  }
};

/// Root constants parameter: a run of 32-bit values bound to a shader register.
///
/// Named to match D3D12_ROOT_CONSTANTS
/// (https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_root_constants).
struct RootConstants {
  /// Shader register the constants are bound to.
  uint32_t ShaderRegister;
  /// Register space of the constants.
  uint32_t RegisterSpace;
  /// Number of 32-bit values in the constants parameter.
  uint32_t Num32BitValues;

  /// Byte-swap multi-byte fields of this parameter.
  void swapBytes() {
    sys::swapByteOrder(ShaderRegister);
    sys::swapByteOrder(RegisterSpace);
    sys::swapByteOrder(Num32BitValues);
  }
};

/// Header describing one parameter in a root signature.
struct RootParameterHeader {
  /// Parameter kind (\c RootParameterType).
  uint32_t ParameterType;
  /// Shader stages that can access this parameter (\c ShaderVisibility).
  uint32_t ShaderVisibility;
  /// Offset from the start of the RTS0 part to this parameter's payload.
  uint32_t ParameterOffset;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() {
    sys::swapByteOrder(ParameterType);
    sys::swapByteOrder(ShaderVisibility);
    sys::swapByteOrder(ParameterOffset);
  }
};

/// Header of a root signature (RTS0) part.
struct RootSignatureHeader {
  /// Serialized root signature version (\c RootSignatureVersion).
  uint32_t Version;
  /// Number of root parameters.
  uint32_t NumParameters;
  /// Offset from the start of the RTS0 part to the parameter header array.
  uint32_t ParametersOffset;
  /// Number of static samplers.
  uint32_t NumStaticSamplers;
  /// Offset from the start of the RTS0 part to the static sampler array.
  uint32_t StaticSamplerOffset;
  /// Root signature flags (\c RootFlags).
  uint32_t Flags;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() {
    sys::swapByteOrder(Version);
    sys::swapByteOrder(NumParameters);
    sys::swapByteOrder(ParametersOffset);
    sys::swapByteOrder(NumStaticSamplers);
    sys::swapByteOrder(StaticSamplerOffset);
    sys::swapByteOrder(Flags);
  }
};
} // namespace v1

/// Version 1.1 root signature records.
namespace v2 {
/// Version 1.1 root descriptor, extending v1 with descriptor flags.
struct RootDescriptor : public v1::RootDescriptor {
  /// Root descriptor flags (\c RootDescriptorFlags).
  uint32_t Flags;

  /// Construct a root descriptor with zeroed flags.
  RootDescriptor() = default;
  /// Construct from a version-1 root descriptor, with flags cleared.
  ///
  /// \param Base Version-1 root descriptor to copy.
  explicit RootDescriptor(v1::RootDescriptor &Base)
      : v1::RootDescriptor(Base), Flags(0u) {}

  /// Byte-swap multi-byte fields of this descriptor.
  void swapBytes() {
    v1::RootDescriptor::swapBytes();
    sys::swapByteOrder(Flags);
  }
};

/// Version 1.1 descriptor range, adding flags to the v1 layout.
struct DescriptorRange {
  /// Descriptor range type (\c dxil::ResourceClass).
  uint32_t RangeType;
  /// Number of descriptors in the range, or -1 for unbounded.
  uint32_t NumDescriptors;
  /// First shader register in the range.
  uint32_t BaseShaderRegister;
  /// Register space of the range.
  uint32_t RegisterSpace;
  /// Descriptor range flags (\c DescriptorRangeFlags).
  uint32_t Flags;
  /// Offset of this range from the start of the descriptor table.
  uint32_t OffsetInDescriptorsFromTableStart;
  /// Byte-swap multi-byte fields of this range.
  void swapBytes() {
    sys::swapByteOrder(RangeType);
    sys::swapByteOrder(NumDescriptors);
    sys::swapByteOrder(BaseShaderRegister);
    sys::swapByteOrder(RegisterSpace);
    sys::swapByteOrder(OffsetInDescriptorsFromTableStart);
    sys::swapByteOrder(Flags);
  }
};
} // namespace v2

/// Version 1.2 root signature records.
namespace v3 {
/// Version 1.2 static sampler, extending v1 with sampler flags.
struct StaticSampler : public v1::StaticSampler {
  /// Static sampler flags (\c StaticSamplerFlags).
  uint32_t Flags;

  /// Construct a static sampler with zeroed flags.
  StaticSampler() = default;
  /// Construct from a version-1 static sampler, with flags cleared.
  ///
  /// \param Base Version-1 static sampler to copy.
  explicit StaticSampler(v1::StaticSampler &Base)
      : v1::StaticSampler(Base), Flags(0U) {}

  /// Byte-swap multi-byte fields of this sampler.
  void swapBytes() {
    v1::StaticSampler::swapBytes();
    sys::swapByteOrder(Flags);
  }
};

} // namespace v3
} // namespace RTS0

/// Serialized D3D12 root signature version identifiers
/// (D3D_ROOT_SIGNATURE_VERSION).
enum class RootSignatureVersion {
  V1_0 = 0x1, ///< Root signature version 1.0.
  V1_1 = 0x2, ///< Root signature version 1.1.
  V1_2 = 0x3, ///< Root signature version 1.2.
};

/// Header of a shader debug-name (ILDN) part.
struct DebugNameHeader {
  /// Reserved flags; must be zero.
  uint16_t Flags;
  /// Debug file name length, without null terminator.
  uint16_t NameLength;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() {
    sys::swapByteOrder(Flags);
    sys::swapByteOrder(NameLength);
  }
};

static_assert(sizeof(DebugNameHeader) == 4, "DebugNameHeader size incorrect.");

#define VERSION_INFO_FLAG(Num, Val, Str) Val = Num,
/// Flags describing how the compiler that produced a VERS part was built.
enum class CompilerVersionFlags : uint32_t {
#include "llvm/BinaryFormat/DXContainerConstants.def"

  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(Internal)
};

/// Return true if \p V is a valid combination of \c CompilerVersionFlags.
///
/// \param V Candidate compiler-version flags bitfield.
/// @return True if \p V is a valid CompilerVersionFlags combination.
LLVM_ABI bool isValidCompilerVersionFlags(uint32_t V);

/// Header of a compiler-version (VERS) part.
struct CompilerVersionHeader {
  /// Compiler major version number.
  uint16_t Major;
  /// Compiler minor version number.
  uint16_t Minor;
  /// Flags describing the compiler build and validation state.
  CompilerVersionFlags Flags;
  /// The number outputted by `git rev-list --count HEAD` in the compiler repo.
  uint32_t CommitCount;
  /// Byte size of CommitSha and CustomVersionString, padding not included.
  uint32_t ContentSizeInBytes;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() {
    sys::swapByteOrder(Major);
    sys::swapByteOrder(Minor);
    sys::swapByteOrder(Flags);
    sys::swapByteOrder(CommitCount);
    sys::swapByteOrder(ContentSizeInBytes);
  }
};

static_assert(sizeof(CompilerVersionHeader) == 16,
              "CompilerVersionHeader size incorrect.");

/// Source-info (SRCI) part layout for compiler inputs.
namespace SourceInfo {

/// Header of a source-info (SRCI) part.
struct Header {
  /// Part size including this header. Aligned to a 4-byte boundary.
  uint32_t AlignedSizeInBytes;
  /// Reserved, must be zero.
  uint16_t Flags;
  /// Source info part must contain 3 sections. Each section is 4-byte aligned.
  uint16_t SectionCount;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() {
    sys::swapByteOrder(AlignedSizeInBytes);
    sys::swapByteOrder(Flags);
    sys::swapByteOrder(SectionCount);
  }
};

static_assert(sizeof(Header) == 8, "SourceInfo::Header size incorrect.");

#define SOURCE_INFO_TYPE(Num, Val) Val = Num,
/// Kinds of sections that may appear in a source-info part.
enum class SectionType : uint16_t {
#include "llvm/BinaryFormat/DXContainerConstants.def"

  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(Args)
};

/// Return the enumerator name table for \c SectionType.
///
/// @return Enumerator name table for SectionType.
LLVM_ABI EnumStrings<SectionType, 1> getSectionTypes();
/// Return true if \p V is a valid \c SectionType value.
///
/// \param V Candidate section-type value.
/// @return True if \p V is a valid SectionType.
LLVM_ABI bool isValidSectionType(uint16_t V);
/// Return the name of \p Type, or an empty StringRef if it is invalid.
///
/// \param Type Source-info section type.
/// @return Section name, or an empty StringRef if \p Type is invalid.
LLVM_ABI StringRef getSectionName(SectionType Type);

/// Header for one section of a source-info part.
struct SectionHeader {
  /// Section size including this header. Aligned to a 4-byte boundary.
  uint32_t AlignedSizeInBytes;
  /// Reserved, must be zero.
  uint16_t Flags;
  /// Kind of data stored in this section.
  SectionType Type;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() {
    sys::swapByteOrder(AlignedSizeInBytes);
    sys::swapByteOrder(Flags);
    sys::swapByteOrder(Type);
  }
};

static_assert(sizeof(SectionHeader) == 8,
              "SourceInfo::SectionHeader size incorrect.");

/// Source-file names recorded in a source-info part.
namespace Names {

/// On-disk header for the source-names section.
LLVM_PACKED(struct HeaderOnDisk {
  /// Reserved, must be zero.
  uint32_t Flags;
  /// The number of data entries.
  uint32_t Count;
  /// The total size of the data entries following this header. Each entry is
  /// 4-byte padded.
  uint16_t EntriesSizeInBytes;
});

static_assert(sizeof(HeaderOnDisk) == 10,
              "SourceInfo::Names::HeaderOnDisk size incorrect.");

/// One source-file name entry.
///
/// Followed by a string of size NameSizeInBytes with the HLSL source file
/// name.
struct Entry {
  /// Size of entry, including this header. Aligned to a 4-byte boundary.
  uint32_t AlignedSizeInBytes;
  /// Reserved, must be set to zero.
  uint32_t Flags;
  /// Size of the file name following this header, including the null
  /// terminator, excluding entry padding.
  uint32_t NameSizeInBytes;
  /// Size of the file content, including the null terminator.
  uint32_t ContentSizeInBytes;

  /// Byte-swap multi-byte fields of this entry.
  void swapBytes() {
    sys::swapByteOrder(AlignedSizeInBytes);
    sys::swapByteOrder(Flags);
    sys::swapByteOrder(NameSizeInBytes);
    sys::swapByteOrder(ContentSizeInBytes);
  }
};

static_assert(sizeof(Entry) == 16, "SourceInfo::Names::Entry size incorrect.");

} // namespace Names

/// Source-file contents recorded in a source-info part.
namespace Contents {

#define COMPRESSION_TYPE(Num, Val) Val = Num,
/// Compression applied to the source-contents section.
enum class CompressionType : uint16_t {
#include "llvm/BinaryFormat/DXContainerConstants.def"

  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(Zlib)
};

/// Return the enumerator name table for \c CompressionType.
///
/// @return Enumerator name table for CompressionType.
LLVM_ABI EnumStrings<CompressionType, 1> getCompressionTypes();
/// Return true if \p V is a valid \c CompressionType value.
///
/// \param V Candidate compression-type value.
/// @return True if \p V is a valid CompressionType.
LLVM_ABI bool isValidCompressionType(uint16_t V);

/// Header for the source-contents section.
struct Header {
  /// Size of the section including this header. Aligned to a 4-byte boundary.
  /// In some implementations, it is unused and set to zero.
  uint32_t AlignedSizeInBytes;
  /// Reserved, must be zero.
  uint16_t Flags;
  /// Type of compression used to compress the whole section content.
  CompressionType Type;
  /// The size of the data entries following this header.
  /// Aligned to a 4-byte boundary if Type is None.
  /// Doesn’t have to be aligned otherwise.
  uint32_t EntriesSizeInBytes;
  /// Total size of the data entries when uncompressed. Aligned to a 4-byte
  /// boundary.
  uint32_t UncompressedEntriesSizeInBytes;
  /// The number of data entries.
  uint32_t Count;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() {
    sys::swapByteOrder(AlignedSizeInBytes);
    sys::swapByteOrder(Flags);
    sys::swapByteOrder(Type);
    sys::swapByteOrder(EntriesSizeInBytes);
    sys::swapByteOrder(UncompressedEntriesSizeInBytes);
    sys::swapByteOrder(Count);
  }
};

static_assert(sizeof(Header) == 20,
              "SourceInfo::Contents::Header size incorrect.");

/// One uncompressed source-file content entry.
///
/// Followed by a string of length ContentSizeInBytes-1 with HLSL source file
/// content. The entry is aligned to 4 bytes (in uncompressed form). Entries of
/// this section must be stored in the same order as the entries of the Names
/// section.
struct Entry {
  /// Size of entry, including this header. Aligned to a 4-byte boundary.
  uint32_t AlignedSizeInBytes;
  /// Reserved, must be zero.
  uint32_t Flags;
  /// Size of the file contents following this header, including the null
  /// terminator, excluding entry padding.
  uint32_t ContentSizeInBytes;

  /// Byte-swap multi-byte fields of this entry.
  void swapBytes() {
    sys::swapByteOrder(AlignedSizeInBytes);
    sys::swapByteOrder(Flags);
    sys::swapByteOrder(ContentSizeInBytes);
  }
};

static_assert(sizeof(Entry) == 12,
              "SourceInfo::Contents::Entry size incorrect.");

} // namespace Contents

/// Compiler command-line arguments recorded in a source-info part.
namespace Args {

/// Header for the compiler-arguments section.
///
/// Followed by Count argument pairs, representing command line arguments of
/// the HLSL compiler invocation. Each pair consists of argument name and
/// argument value null-terminated strings. Padding is not applied to the
/// pairs.
struct Header {
  /// Reserved, must be zero.
  uint32_t Flags;
  /// Length of all argument pairs, including their null terminators, not
  /// including this header.
  uint32_t SizeInBytes;
  /// Number of arguments.
  uint32_t Count;

  /// Byte-swap multi-byte fields of this header.
  void swapBytes() {
    sys::swapByteOrder(Flags);
    sys::swapByteOrder(SizeInBytes);
    sys::swapByteOrder(Count);
  }
};

static_assert(sizeof(Header) == 12, "SourceInfo::Args::Header size incorrect.");

} // namespace Args

} // namespace SourceInfo

} // namespace dxbc
} // namespace llvm

#endif // LLVM_BINARYFORMAT_DXCONTAINER_H
