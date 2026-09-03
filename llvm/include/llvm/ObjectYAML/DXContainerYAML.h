//===- DXContainerYAML.h - DXContainer YAMLIO implementation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation
/// of DXContainer.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_DXCONTAINERYAML_H
#define LLVM_OBJECTYAML_DXCONTAINERYAML_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Object/DXContainer.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/YAMLTraits.h"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
/// YAML representations of DXContainer binaries.
namespace DXContainerYAML {

/// Major and minor version pair used in DXContainer YAML.
struct VersionTuple {
  /// Major version number.
  uint16_t Major;
  /// Minor version number.
  uint16_t Minor;
};

/// YAML representation of a DXContainer file header.
///
/// The optional header fields are required in the binary and will be populated
/// when reading from binary, but can be omitted in the YAML text because the
/// emitter can calculate them.
struct FileHeader {
  /// Optional file hash bytes.
  std::vector<llvm::yaml::Hex8> Hash;
  /// Container format version.
  VersionTuple Version;
  /// Optional total file size in bytes.
  std::optional<uint32_t> FileSize;
  /// Number of parts in the container.
  uint32_t PartCount;
  /// Optional byte offsets of each part within the file.
  std::optional<std::vector<uint32_t>> PartOffsets;
};

/// YAML representation of a DXIL program part.
struct DXILProgram {
  /// Shader model major version.
  uint8_t MajorVersion;
  /// Shader model minor version.
  uint8_t MinorVersion;
  /// Shader stage / kind identifier.
  uint16_t ShaderKind;
  /// Optional size of the program structure in bytes.
  std::optional<uint32_t> Size;
  /// DXIL bitcode major version.
  uint16_t DXILMajorVersion;
  /// DXIL bitcode minor version.
  uint16_t DXILMinorVersion;
  /// Optional offset of the DXIL bitcode within the part.
  std::optional<uint32_t> DXILOffset;
  /// Optional size of the DXIL bitcode in bytes.
  std::optional<uint32_t> DXILSize;
  /// Optional raw DXIL bitcode bytes.
  std::optional<std::vector<llvm::yaml::Hex8>> DXIL;
};

#define SHADER_FEATURE_FLAG(Num, DxilModuleNum, Val, Str) bool Val = false;
/// YAML representation of shader feature flags.
struct ShaderFeatureFlags {
  /// Construct default (cleared) feature flags.
  ShaderFeatureFlags() = default;
  /// Construct feature flags from an encoded bitmask.
  /// \param FlagData Encoded feature-flag bits.
  LLVM_ABI ShaderFeatureFlags(uint64_t FlagData);
  /// Return the feature flags packed into a bitmask.
  /// \return Encoded feature-flag bits.
  LLVM_ABI uint64_t getEncodedFlags();
#include "llvm/BinaryFormat/DXContainerConstants.def"
};

/// YAML representation of a shader hash.
struct ShaderHash {
  /// Construct a default (empty) shader hash.
  ShaderHash() = default;
  /// Construct from a native DXBC shader hash.
  /// \param Data Native shader hash to convert.
  LLVM_ABI ShaderHash(const dxbc::ShaderHash &Data);

  /// True when the hash includes source information.
  bool IncludesSource;
  /// Digest bytes of the shader hash.
  std::vector<llvm::yaml::Hex8> Digest;
};

/// YAML representation of root signature constants.
struct RootConstantsYaml {
  /// Shader register bound by these constants.
  uint32_t ShaderRegister;
  /// Register space of the bound register.
  uint32_t RegisterSpace;
  /// Number of 32-bit constant values.
  uint32_t Num32BitValues;
};

/// YAML representation of a root signature descriptor.
struct RootDescriptorYaml {
  /// Construct a default root descriptor.
  RootDescriptorYaml() = default;

  /// Shader register bound by this descriptor.
  uint32_t ShaderRegister;
  /// Register space of the bound register.
  uint32_t RegisterSpace;

  /// Return the descriptor flags packed into a bitmask.
  /// \return Encoded descriptor-flag bits.
  LLVM_ABI uint32_t getEncodedFlags() const;

#define ROOT_DESCRIPTOR_FLAG(Num, Enum, Flag) bool Enum = false;
#include "llvm/BinaryFormat/DXContainerConstants.def"
};

/// YAML representation of a descriptor-table range.
struct DescriptorRangeYaml {
  /// Resource class of descriptors in this range.
  dxil::ResourceClass RangeType;
  /// Number of descriptors in the range.
  uint32_t NumDescriptors;
  /// First shader register covered by the range.
  uint32_t BaseShaderRegister;
  /// Register space of the range.
  uint32_t RegisterSpace;
  /// Offset of the range within the descriptor table.
  uint32_t OffsetInDescriptorsFromTableStart;

  /// Return the descriptor-range flags packed into a bitmask.
  /// \return Encoded descriptor-range flag bits.
  LLVM_ABI uint32_t getEncodedFlags() const;

#define DESCRIPTOR_RANGE_FLAG(Num, Enum, Flag) bool Enum = false;
#include "llvm/BinaryFormat/DXContainerConstants.def"
};

/// YAML representation of a root signature descriptor table.
struct DescriptorTableYaml {
  /// Number of ranges in the table.
  uint32_t NumRanges;
  /// Offset of the ranges array within the root signature.
  uint32_t RangesOffset;
  /// Descriptor ranges that make up the table.
  SmallVector<DescriptorRangeYaml> Ranges;
};

/// YAML representation of a root-parameter header.
struct RootParameterHeaderYaml {
  /// Kind of root parameter described by this header.
  dxbc::RootParameterType Type;
  /// Shader stages that can see this parameter.
  dxbc::ShaderVisibility Visibility;
  /// Offset of the parameter payload within the root signature.
  uint32_t Offset;

  /// Construct a default root-parameter header.
  RootParameterHeaderYaml() = default;
  /// Construct a header for parameter type \p T.
  /// \param T Root parameter type to store.
  RootParameterHeaderYaml(dxbc::RootParameterType T) : Type(T) {}
};

/// YAML representation of a root parameter's location in the signature.
struct RootParameterLocationYaml {
  /// Header describing the parameter type and visibility.
  RootParameterHeaderYaml Header;
  /// Optional index into the matching typed parameter list.
  std::optional<size_t> IndexInSignature;

  /// Construct a default root-parameter location.
  RootParameterLocationYaml() = default;
  /// Construct a location with the given header.
  /// \param Header Header describing the parameter.
  explicit RootParameterLocationYaml(RootParameterHeaderYaml Header)
      : Header(Header) {}
};

/// YAML representation of root-parameter payloads for a root signature.
struct RootParameterYamlDesc {
  /// Ordered list of root-parameter locations.
  SmallVector<RootParameterLocationYaml> Locations;

  /// Root-constant parameter payloads.
  SmallVector<RootConstantsYaml> Constants;
  /// Root-descriptor parameter payloads.
  SmallVector<RootDescriptorYaml> Descriptors;
  /// Descriptor-table parameter payloads.
  SmallVector<DescriptorTableYaml> Tables;

  /// Return the payload at \p ParamDesc, inserting a default if needed.
  /// \param ParamDesc Location whose typed payload is requested.
  /// \param Container Typed payload vector to search or extend.
  /// \return Reference to the typed payload for \p ParamDesc.
  template <typename T>
  T &getOrInsertImpl(RootParameterLocationYaml &ParamDesc,
                     SmallVectorImpl<T> &Container) {
    if (!ParamDesc.IndexInSignature) {
      ParamDesc.IndexInSignature = Container.size();
      Container.emplace_back();
    }
    return Container[*ParamDesc.IndexInSignature];
  }

  /// Return the root-constants payload for \p ParamDesc.
  /// \param ParamDesc Location whose constants payload is requested.
  /// \return Reference to the constants payload.
  RootConstantsYaml &
  getOrInsertConstants(RootParameterLocationYaml &ParamDesc) {
    return getOrInsertImpl(ParamDesc, Constants);
  }

  /// Return the root-descriptor payload for \p ParamDesc.
  /// \param ParamDesc Location whose descriptor payload is requested.
  /// \return Reference to the descriptor payload.
  RootDescriptorYaml &
  getOrInsertDescriptor(RootParameterLocationYaml &ParamDesc) {
    return getOrInsertImpl(ParamDesc, Descriptors);
  }

  /// Return the descriptor-table payload for \p ParamDesc.
  /// \param ParamDesc Location whose table payload is requested.
  /// \return Reference to the descriptor-table payload.
  DescriptorTableYaml &getOrInsertTable(RootParameterLocationYaml &ParamDesc) {
    return getOrInsertImpl(ParamDesc, Tables);
  }

  /// Append \p Location to the ordered location list.
  /// \param Location Root-parameter location to append.
  void insertLocation(RootParameterLocationYaml &Location) {
    Locations.push_back(Location);
  }
};

/// YAML representation of a static sampler in a root signature.
struct StaticSamplerYamlDesc {
  /// Texture filtering mode.
  dxbc::SamplerFilter Filter = dxbc::SamplerFilter::Anisotropic;
  /// Addressing mode for the U texture coordinate.
  dxbc::TextureAddressMode AddressU = dxbc::TextureAddressMode::Wrap;
  /// Addressing mode for the V texture coordinate.
  dxbc::TextureAddressMode AddressV = dxbc::TextureAddressMode::Wrap;
  /// Addressing mode for the W texture coordinate.
  dxbc::TextureAddressMode AddressW = dxbc::TextureAddressMode::Wrap;
  /// MIP level-of-detail bias.
  float MipLODBias = 0.f;
  /// Maximum anisotropy value.
  uint32_t MaxAnisotropy = 16u;
  /// Comparison function used by comparison samplers.
  dxbc::ComparisonFunc ComparisonFunc = dxbc::ComparisonFunc::LessEqual;
  /// Border color used for border addressing.
  dxbc::StaticBorderColor BorderColor = dxbc::StaticBorderColor::OpaqueWhite;
  /// Minimum level of detail.
  float MinLOD = 0.f;
  /// Maximum level of detail.
  float MaxLOD = std::numeric_limits<float>::max();
  /// Shader register bound by this sampler.
  uint32_t ShaderRegister;
  /// Register space of the bound register.
  uint32_t RegisterSpace;
  /// Shader stages that can see this sampler.
  dxbc::ShaderVisibility ShaderVisibility;

  /// Return the static-sampler flags packed into a bitmask.
  /// \return Encoded static-sampler flag bits.
  LLVM_ABI uint32_t getEncodedFlags() const;

#define STATIC_SAMPLER_FLAG(Num, Enum, Flag) bool Enum = false;
#include "llvm/BinaryFormat/DXContainerConstants.def"
};

/// YAML representation of a DXContainer root signature.
struct RootSignatureYamlDesc {
  /// Construct a default root signature description.
  RootSignatureYamlDesc() = default;

  /// Root signature format version.
  uint32_t Version;
  /// Number of root parameters.
  uint32_t NumRootParameters;
  /// Optional offset of the root-parameter array.
  std::optional<uint32_t> RootParametersOffset;
  /// Number of static samplers.
  uint32_t NumStaticSamplers;
  /// Optional offset of the static-sampler array.
  std::optional<uint32_t> StaticSamplersOffset;

  /// Root-parameter payloads and locations.
  RootParameterYamlDesc Parameters;
  /// Static samplers declared by the root signature.
  SmallVector<StaticSamplerYamlDesc> StaticSamplers;

  /// Return the root-signature flags packed into a bitmask.
  /// \return Encoded root-signature flag bits.
  LLVM_ABI uint32_t getEncodedFlags();

  /// Return an iterator range over the static samplers.
  /// \return Iterator range over the static samplers.
  iterator_range<StaticSamplerYamlDesc *> samplers() {
    return make_range(StaticSamplers.begin(), StaticSamplers.end());
  }

  /// Build a YAML root signature from a parsed DirectX root signature.
  /// \param Data Parsed DirectX root signature to convert.
  /// \return YAML root signature, or an error on failure.
  LLVM_ABI static llvm::Expected<DXContainerYAML::RootSignatureYamlDesc>
  create(const object::DirectX::RootSignature &Data);

#define ROOT_SIGNATURE_FLAG(Num, Val) bool Val = false;
#include "llvm/BinaryFormat/DXContainerConstants.def"
};

/// Alias for PSV resource flags used in YAML.
using ResourceFlags = dxbc::PSV::ResourceFlags;
/// Alias for PSV v2 resource bind info used in YAML.
using ResourceBindInfo = dxbc::PSV::v2::ResourceBindInfo;

/// YAML representation of a PSV signature element.
struct SignatureElement {
  /// Construct a default signature element.
  SignatureElement() = default;

  /// Construct from a native PSV v0 signature element.
  /// \param El Native signature element to convert.
  /// \param StringTable String table used to resolve the element name.
  /// \param IdxTable Index table used to resolve semantic indices.
  SignatureElement(dxbc::PSV::v0::SignatureElement El, StringRef StringTable,
                   ArrayRef<uint32_t> IdxTable)
      : Name(StringTable.substr(El.NameOffset,
                                StringTable.find('\0', El.NameOffset) -
                                    El.NameOffset)),
        Indices(IdxTable.slice(El.IndicesOffset, El.Rows)),
        StartRow(El.StartRow), Cols(El.Cols), StartCol(El.StartCol),
        Allocated(El.Allocated != 0), Kind(El.Kind), Type(El.Type),
        Mode(El.Mode), DynamicMask(El.DynamicMask), Stream(El.Stream) {}
  /// Semantic name of this signature element.
  StringRef Name;
  /// Semantic indices associated with this element.
  SmallVector<uint32_t> Indices;

  /// Starting row in the signature register grid.
  uint8_t StartRow;
  /// Number of columns occupied by this element.
  uint8_t Cols;
  /// Starting column in the signature register grid.
  uint8_t StartCol;
  /// True when this element is allocated to a register.
  bool Allocated;
  /// Semantic kind of this element.
  dxbc::PSV::SemanticKind Kind;

  /// Component type of this element.
  dxbc::PSV::ComponentType Type;
  /// Interpolation mode of this element.
  dxbc::PSV::InterpolationMode Mode;
  /// Dynamic mask bits for this element.
  llvm::yaml::Hex8 DynamicMask;
  /// Output stream index for this element.
  uint8_t Stream;
};

/// YAML representation of one string-table entry.
struct StringTableEntry {
  /// String value stored at \c Offset.
  StringRef String;
  /// Byte offset of the string within the table.
  uint32_t Offset;
};

/// YAML representation of pipeline state validation (PSV) info.
struct PSVInfo {
  /// Inferred PSV format version based on data-region sizes.
  ///
  /// The version field isn't actually encoded in the file, but it is inferred
  /// by the size of data regions. We include it in the yaml because it
  /// simplifies the format.
  uint32_t Version;

  /// Runtime info fields for the newest supported PSV version.
  dxbc::PSV::v3::RuntimeInfo Info;
  /// Stride in bytes of each resource bind-info record.
  uint32_t ResourceStride;
  /// Resource bind-info records.
  SmallVector<ResourceBindInfo> Resources;
  /// Input signature elements.
  SmallVector<SignatureElement> SigInputElements;
  /// Output signature elements.
  SmallVector<SignatureElement> SigOutputElements;
  /// Patch or primitive signature elements.
  SmallVector<SignatureElement> SigPatchOrPrimElements;

  /// Vector of 32-bit mask values used by PSV view maps.
  using MaskVector = SmallVector<llvm::yaml::Hex32>;
  /// Per-output-stream vector masks.
  std::array<MaskVector, 4> OutputVectorMasks;
  /// Patch or primitive vector masks.
  MaskVector PatchOrPrimMasks;
  /// Input-to-output dependency maps per stream.
  std::array<MaskVector, 4> InputOutputMap;
  /// Input-to-patch dependency map.
  MaskVector InputPatchMap;
  /// Patch-to-output dependency map.
  MaskVector PatchOutputMap;

  /// Entry-point name resolved from the string table.
  StringRef EntryName;

  /// String-table entries populated by obj2yaml for inspection.
  SmallVector<StringTableEntry> StringTable;
  /// Size in bytes of the runtime info structure.
  uint32_t RuntimeInfoSize = 0;

  /// Map version-dependent PSV fields to and from YAML.
  /// \param IO YAML input/output state.
  LLVM_ABI void mapInfoForVersion(yaml::IO &IO);

  /// Construct default PSV info.
  LLVM_ABI PSVInfo();
  /// Construct PSV info from a v0 runtime info record.
  /// \param P Native v0 runtime info pointer.
  /// \param Stage Shader stage associated with the info.
  LLVM_ABI PSVInfo(const dxbc::PSV::v0::RuntimeInfo *P, uint16_t Stage);
  /// Construct PSV info from a v1 runtime info record.
  /// \param P Native v1 runtime info pointer.
  LLVM_ABI PSVInfo(const dxbc::PSV::v1::RuntimeInfo *P);
  /// Construct PSV info from a v2 runtime info record.
  /// \param P Native v2 runtime info pointer.
  LLVM_ABI PSVInfo(const dxbc::PSV::v2::RuntimeInfo *P);
  /// Construct PSV info from a v3 runtime info record.
  /// \param P Native v3 runtime info pointer.
  /// \param StringTable String table used to resolve names.
  LLVM_ABI PSVInfo(const dxbc::PSV::v3::RuntimeInfo *P, StringRef StringTable);
};

/// YAML representation of one signature parameter.
struct SignatureParameter {
  /// Stream index for this parameter.
  uint32_t Stream;
  /// Semantic name of this parameter.
  std::string Name;
  /// Semantic index of this parameter.
  uint32_t Index;
  /// System-value semantic, if any.
  dxbc::D3DSystemValue SystemValue;
  /// Component type of this parameter.
  dxbc::SigComponentType CompType;
  /// Register assigned to this parameter.
  uint32_t Register;
  /// Component mask used by this parameter.
  uint8_t Mask;
  /// Exclusive component mask used by this parameter.
  uint8_t ExclusiveMask;
  /// Minimum precision of this parameter.
  dxbc::SigMinPrecision MinPrecision;
};

/// YAML representation of an input or output signature part.
struct Signature {
  /// Signature parameters in declaration order.
  llvm::SmallVector<SignatureParameter> Parameters;
};

/// YAML representation of a debug-name part.
struct DebugName {
  /// Optional flags associated with the debug name.
  std::optional<uint16_t> Flags;
  /// Optional length of the debug file name.
  std::optional<uint16_t> NameLength;
  /// Debug file name stored in the part.
  std::string Filename;
};

/// YAML representation of a compiler-version part.
struct CompilerVersion {
  /// Optional compiler major version.
  std::optional<uint16_t> Major;
  /// Optional compiler minor version.
  std::optional<uint16_t> Minor;
  /// Optional flag indicating a debug build of the compiler.
  std::optional<bool> IsDebugBuild;
  /// Optional flag indicating the compiler build was validated.
  std::optional<bool> IsValidated;
  /// Optional commit count of the compiler build.
  std::optional<uint32_t> CommitCount;
  /// Optional size in bytes of the version payload.
  std::optional<uint32_t> ContentSizeInBytes;
  /// Optional commit SHA of the compiler build.
  std::optional<std::string> CommitSha;
  /// Optional custom version string.
  std::optional<std::string> CustomVersionString;
};

/// YAML representation of a source-info part.
struct SourceInfo {
  /// YAML representation of the source-info part header.
  struct Header {
    /// Optional aligned size of the source-info part in bytes.
    std::optional<uint32_t> AlignedSizeInBytes;
    /// Optional flags for the source-info part.
    std::optional<uint16_t> Flags;
    /// Optional number of sections in the source-info part.
    std::optional<uint16_t> SectionCount;
  };

  /// YAML representation of a generic source-info section header.
  struct SectionHeader {
    /// Optional aligned size of the section in bytes.
    std::optional<uint32_t> AlignedSizeInBytes;
    /// Optional flags for the section.
    std::optional<uint16_t> Flags;
    /// Optional section type tag.
    std::optional<dxbc::SourceInfo::SectionType> Type;
  };

  /// YAML representation of a generic source-info section.
  struct Section {
    /// Generic header shared by all section kinds.
    SectionHeader GenericHeader;
  };

  /// YAML representation of a source-contents section.
  struct SourceContents : public Section {
    /// YAML representation of a source-contents section header.
    struct Header {
      /// Optional aligned size of the section in bytes.
      std::optional<uint32_t> AlignedSizeInBytes;
      /// Optional flags for the section.
      std::optional<uint16_t> Flags;
      /// Compression type used for the contents entries.
      dxbc::SourceInfo::Contents::CompressionType Type;
      /// Optional compressed size of all entries in bytes.
      std::optional<uint32_t> EntriesSizeInBytes;
      /// Optional uncompressed size of all entries in bytes.
      std::optional<uint32_t> UncompressedEntriesSizeInBytes;
      /// Optional number of content entries.
      std::optional<uint32_t> Count;
    };

    /// YAML representation of one source-contents entry.
    struct Entry {
      /// Optional aligned size of this entry in bytes.
      std::optional<uint32_t> AlignedSizeInBytes;
      /// Optional flags for this entry.
      std::optional<uint32_t> Flags;
      /// Optional size of the file content in bytes.
      std::optional<uint32_t> ContentSizeInBytes;
      /// Source file contents.
      std::string FileContent;
    };

    /// Header parameters for the source-contents section.
    Header Parameters;
    /// Source content entries.
    SmallVector<Entry> Entries;
  };

  /// YAML representation of a source-names section.
  struct SourceNames : public Section {
    /// YAML representation of a source-names section header.
    struct Header {
      /// Optional flags for the section.
      std::optional<uint32_t> Flags;
      /// Optional number of name entries.
      std::optional<uint32_t> Count;
      /// Optional size of all name entries in bytes.
      std::optional<uint16_t> EntriesSizeInBytes;
    };

    /// YAML representation of one source-name entry.
    struct Entry {
      /// Optional aligned size of this entry in bytes.
      std::optional<uint32_t> AlignedSizeInBytes;
      /// Optional flags for this entry.
      std::optional<uint32_t> Flags;
      /// Optional size of the file name in bytes.
      std::optional<uint32_t> NameSizeInBytes;
      /// Optional size of the associated content in bytes.
      std::optional<uint32_t> ContentSizeInBytes;
      /// Source file name.
      StringRef FileName;
    };

    /// Header parameters for the source-names section.
    Header Parameters;
    /// Source name entries.
    SmallVector<Entry> Entries;
  };

  /// YAML representation of a program-arguments section.
  struct ProgramArgs : public Section {
    /// YAML representation of a program-arguments section header.
    struct Header {
      /// Optional flags for the section.
      std::optional<uint32_t> Flags;
      /// Optional size of the section payload in bytes.
      std::optional<uint32_t> SizeInBytes;
      /// Optional number of argument entries.
      std::optional<uint32_t> Count;
    };

    /// Header parameters for the program-arguments section.
    Header Parameters;
    /// Program argument entries.
    SmallVector<mcdxbc::SourceInfo::ProgramArgs::Entry> Args;
  };

  /// Top-level source-info header parameters.
  Header Parameters;
  /// Source-names section.
  SourceNames Names;
  /// Source-contents section.
  SourceContents Contents;
  /// Program-arguments section.
  ProgramArgs Args;
};

/// YAML representation of one DXContainer part.
struct Part {
  /// Construct a default (empty) part.
  Part() = default;
  /// Construct a part with the given name and size.
  /// \param N Four-character part name.
  /// \param S Declared size of the part in bytes.
  Part(std::string N, uint32_t S) : Name(N), Size(S) {}
  /// Four-character part name.
  std::string Name;
  /// Declared size of the part in bytes.
  uint32_t Size;
  /// Optional DXIL program payload.
  std::optional<DXILProgram> Program;
  /// Optional shader feature flags payload.
  std::optional<ShaderFeatureFlags> Flags;
  /// Optional shader hash payload.
  std::optional<ShaderHash> Hash;
  /// Optional pipeline state validation info payload.
  std::optional<PSVInfo> Info;
  /// Optional input/output signature payload.
  std::optional<DXContainerYAML::Signature> Signature;
  /// Optional root signature payload.
  std::optional<DXContainerYAML::RootSignatureYamlDesc> RootSignature;
  /// Optional debug-name payload.
  std::optional<DXContainerYAML::DebugName> DebugName;
  /// Optional compiler-version payload.
  std::optional<DXContainerYAML::CompilerVersion> CompilerVersion;
  /// Optional source-info payload.
  std::optional<DXContainerYAML::SourceInfo> SourceInfo;
  /// Optional private data bytes for unrecognized parts.
  std::optional<std::vector<llvm::yaml::Hex8>> PrivateData;
};

/// YAML representation of a complete DXContainer object.
struct Object {
  /// File header for the container.
  FileHeader Header;
  /// Parts that make up the container.
  std::vector<Part> Parts;
};

/// Build a YAML DXContainer object from a parsed binary container.
/// \param DXC Parsed DXContainer to convert.
/// \return YAML object, or an error on failure.
LLVM_ABI Expected<std::unique_ptr<DXContainerYAML::Object>>
fromDXContainer(object::DXContainer &DXC);

} // namespace DXContainerYAML
} // namespace llvm

namespace llvm {
namespace yaml {

/// Sequences of DXContainerYAML parts use block formatting.
template <> struct SequenceElementTraits<llvm::DXContainerYAML::Part> {
  /// Emit sequences of DXContainerYAML parts in block style.
  static const bool flow = false;
};

/// Sequences of DXContainerYAML resource bind infos use block formatting.
template <>
struct SequenceElementTraits<llvm::DXContainerYAML::ResourceBindInfo> {
  /// Emit sequences of resource bind infos in block style.
  static const bool flow = false;
};

/// Sequences of DXContainerYAML signature elements use block formatting.
template <>
struct SequenceElementTraits<llvm::DXContainerYAML::SignatureElement> {
  /// Emit sequences of signature elements in block style.
  static const bool flow = false;
};

/// Sequences of DXContainerYAML PSV mask vectors use block formatting.
template <>
struct SequenceElementTraits<llvm::DXContainerYAML::PSVInfo::MaskVector> {
  /// Emit sequences of PSV mask vectors in block style.
  static const bool flow = false;
};

/// Sequences of DXContainerYAML signature parameters use block formatting.
template <>
struct SequenceElementTraits<llvm::DXContainerYAML::SignatureParameter> {
  /// Emit sequences of signature parameters in block style.
  static const bool flow = false;
};

/// Sequences of root-parameter locations use block formatting.
template <>
struct SequenceElementTraits<llvm::DXContainerYAML::RootParameterLocationYaml> {
  /// Emit sequences of root-parameter locations in block style.
  static const bool flow = false;
};

/// Sequences of descriptor ranges use block formatting.
template <>
struct SequenceElementTraits<llvm::DXContainerYAML::DescriptorRangeYaml> {
  /// Emit sequences of descriptor ranges in block style.
  static const bool flow = false;
};

/// Sequences of static samplers use block formatting.
template <>
struct SequenceElementTraits<llvm::DXContainerYAML::StaticSamplerYamlDesc> {
  /// Emit sequences of static samplers in block style.
  static const bool flow = false;
};

/// Sequences of string-table entries use block formatting.
template <>
struct SequenceElementTraits<llvm::DXContainerYAML::StringTableEntry> {
  /// Emit sequences of string-table entries in block style.
  static const bool flow = false;
};

/// Sequences of source-name entries use block formatting.
template <>
struct SequenceElementTraits<
    llvm::DXContainerYAML::SourceInfo::SourceNames::Entry> {
  /// Emit sequences of source-name entries in block style.
  static const bool flow = false;
};

/// Sequences of source-content entries use block formatting.
template <>
struct SequenceElementTraits<
    llvm::DXContainerYAML::SourceInfo::SourceContents::Entry> {
  /// Emit sequences of source-content entries in block style.
  static const bool flow = false;
};

/// Sequences of program-argument entries use block formatting.
template <>
struct SequenceElementTraits<mcdxbc::SourceInfo::ProgramArgs::Entry> {
  /// Emit sequences of program-argument entries in block style.
  static const bool flow = false;
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::PSV::SemanticKind.
template <> struct ScalarEnumerationTraits<llvm::dxbc::PSV::SemanticKind> {
  /// Map PSV semantic-kind enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Semantic kind being mapped.
  LLVM_ABI static void enumeration(IO &IO, llvm::dxbc::PSV::SemanticKind &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::PSV::ComponentType.
template <> struct ScalarEnumerationTraits<llvm::dxbc::PSV::ComponentType> {
  /// Map PSV component-type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Component type being mapped.
  LLVM_ABI static void enumeration(IO &IO,
                                   llvm::dxbc::PSV::ComponentType &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::PSV::InterpolationMode.
template <>
struct ScalarEnumerationTraits<llvm::dxbc::PSV::InterpolationMode> {
  /// Map PSV interpolation-mode enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Interpolation mode being mapped.
  LLVM_ABI static void enumeration(IO &IO,
                                   llvm::dxbc::PSV::InterpolationMode &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::PSV::ResourceType.
template <> struct ScalarEnumerationTraits<llvm::dxbc::PSV::ResourceType> {
  /// Map PSV resource-type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Resource type being mapped.
  LLVM_ABI static void enumeration(IO &IO, llvm::dxbc::PSV::ResourceType &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::PSV::ResourceKind.
template <> struct ScalarEnumerationTraits<llvm::dxbc::PSV::ResourceKind> {
  /// Map PSV resource-kind enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Resource kind being mapped.
  LLVM_ABI static void enumeration(IO &IO, llvm::dxbc::PSV::ResourceKind &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::D3DSystemValue.
template <> struct ScalarEnumerationTraits<llvm::dxbc::D3DSystemValue> {
  /// Map D3D system-value enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value System value being mapped.
  LLVM_ABI static void enumeration(IO &IO, llvm::dxbc::D3DSystemValue &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::SigComponentType.
template <> struct ScalarEnumerationTraits<llvm::dxbc::SigComponentType> {
  /// Map signature component-type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Component type being mapped.
  LLVM_ABI static void enumeration(IO &IO, llvm::dxbc::SigComponentType &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::SigMinPrecision.
template <> struct ScalarEnumerationTraits<llvm::dxbc::SigMinPrecision> {
  /// Map signature minimum-precision enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Minimum precision being mapped.
  LLVM_ABI static void enumeration(IO &IO, llvm::dxbc::SigMinPrecision &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::RootParameterType.
template <> struct ScalarEnumerationTraits<llvm::dxbc::RootParameterType> {
  /// Map root-parameter type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Root parameter type being mapped.
  LLVM_ABI static void enumeration(IO &IO,
                                   llvm::dxbc::RootParameterType &Value);
};

/// YAMLIO scalar enumeration traits for \c dxil::ResourceClass.
template <> struct ScalarEnumerationTraits<dxil::ResourceClass> {
  /// Map DXIL resource-class enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Resource class being mapped.
  LLVM_ABI static void enumeration(IO &IO, dxil::ResourceClass &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::SamplerFilter.
template <> struct ScalarEnumerationTraits<llvm::dxbc::SamplerFilter> {
  /// Map sampler-filter enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Sampler filter being mapped.
  LLVM_ABI static void enumeration(IO &IO, llvm::dxbc::SamplerFilter &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::StaticBorderColor.
template <> struct ScalarEnumerationTraits<llvm::dxbc::StaticBorderColor> {
  /// Map static border-color enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Border color being mapped.
  LLVM_ABI static void enumeration(IO &IO,
                                   llvm::dxbc::StaticBorderColor &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::TextureAddressMode.
template <> struct ScalarEnumerationTraits<llvm::dxbc::TextureAddressMode> {
  /// Map texture address-mode enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Texture address mode being mapped.
  LLVM_ABI static void enumeration(IO &IO,
                                   llvm::dxbc::TextureAddressMode &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::ShaderVisibility.
template <> struct ScalarEnumerationTraits<llvm::dxbc::ShaderVisibility> {
  /// Map shader-visibility enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Shader visibility being mapped.
  LLVM_ABI static void enumeration(IO &IO, llvm::dxbc::ShaderVisibility &Value);
};

/// YAMLIO scalar enumeration traits for \c llvm::dxbc::ComparisonFunc.
template <> struct ScalarEnumerationTraits<llvm::dxbc::ComparisonFunc> {
  /// Map comparison-function enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Comparison function being mapped.
  LLVM_ABI static void enumeration(IO &IO, llvm::dxbc::ComparisonFunc &Value);
};

/// YAMLIO scalar enumeration traits for
/// \c llvm::dxbc::SourceInfo::SectionType.
template <>
struct ScalarEnumerationTraits<llvm::dxbc::SourceInfo::SectionType> {
  /// Map source-info section-type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Section type being mapped.
  LLVM_ABI static void enumeration(IO &IO,
                                   llvm::dxbc::SourceInfo::SectionType &Value);
};

/// YAMLIO scalar enumeration traits for
/// \c llvm::dxbc::SourceInfo::Contents::CompressionType.
template <>
struct ScalarEnumerationTraits<
    llvm::dxbc::SourceInfo::Contents::CompressionType> {
  /// Map source-contents compression-type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Compression type being mapped.
  LLVM_ABI static void
  enumeration(IO &IO, llvm::dxbc::SourceInfo::Contents::CompressionType &Value);
};

/// YAMLIO mapping traits for \c DXContainerYAML::VersionTuple.
template <> struct MappingTraits<DXContainerYAML::VersionTuple> {
  /// Map version-tuple fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Version Version tuple being mapped.
  LLVM_ABI static void mapping(IO &IO, DXContainerYAML::VersionTuple &Version);
};

/// YAMLIO mapping traits for \c DXContainerYAML::FileHeader.
template <> struct MappingTraits<DXContainerYAML::FileHeader> {
  /// Map DXContainer file-header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Header File header being mapped.
  LLVM_ABI static void mapping(IO &IO, DXContainerYAML::FileHeader &Header);
};

/// YAMLIO mapping traits for \c DXContainerYAML::DXILProgram.
template <> struct MappingTraits<DXContainerYAML::DXILProgram> {
  /// Map DXIL program fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Program DXIL program being mapped.
  LLVM_ABI static void mapping(IO &IO, DXContainerYAML::DXILProgram &Program);
};

/// YAMLIO mapping traits for \c DXContainerYAML::ShaderFeatureFlags.
template <> struct MappingTraits<DXContainerYAML::ShaderFeatureFlags> {
  /// Map shader feature-flag fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Flags Feature flags being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               DXContainerYAML::ShaderFeatureFlags &Flags);
};

/// YAMLIO mapping traits for \c DXContainerYAML::ShaderHash.
template <> struct MappingTraits<DXContainerYAML::ShaderHash> {
  /// Map shader-hash fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Hash Shader hash being mapped.
  LLVM_ABI static void mapping(IO &IO, DXContainerYAML::ShaderHash &Hash);
};

/// YAMLIO mapping traits for \c DXContainerYAML::PSVInfo.
template <> struct MappingTraits<DXContainerYAML::PSVInfo> {
  /// Map PSV info fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param PSV PSV info being mapped.
  LLVM_ABI static void mapping(IO &IO, DXContainerYAML::PSVInfo &PSV);
};

/// YAMLIO mapping traits for \c DXContainerYAML::DebugName.
template <> struct MappingTraits<DXContainerYAML::DebugName> {
  /// Map debug-name fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param DebugName Debug name being mapped.
  LLVM_ABI static void mapping(IO &IO, DXContainerYAML::DebugName &DebugName);
};

/// YAMLIO mapping traits for \c DXContainerYAML::CompilerVersion.
template <> struct MappingTraits<DXContainerYAML::CompilerVersion> {
  /// Map compiler-version fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param CompilerVersion Compiler version being mapped.
  LLVM_ABI static void
  mapping(IO &IO, DXContainerYAML::CompilerVersion &CompilerVersion);
};

/// YAMLIO mapping traits for \c DXContainerYAML::Part.
template <> struct MappingTraits<DXContainerYAML::Part> {
  /// Map DXContainer part fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Version Part being mapped.
  LLVM_ABI static void mapping(IO &IO, DXContainerYAML::Part &Version);
};

/// YAMLIO mapping traits for \c DXContainerYAML::Object.
template <> struct MappingTraits<DXContainerYAML::Object> {
  /// Map DXContainer object fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Object being mapped.
  LLVM_ABI static void mapping(IO &IO, DXContainerYAML::Object &Obj);
};

/// YAMLIO mapping traits for \c DXContainerYAML::ResourceFlags.
template <> struct MappingTraits<DXContainerYAML::ResourceFlags> {
  /// Map PSV resource-flag fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Flags Resource flags being mapped.
  LLVM_ABI static void mapping(IO &IO, DXContainerYAML::ResourceFlags &Flags);
};

/// YAMLIO mapping traits for \c DXContainerYAML::ResourceBindInfo.
template <> struct MappingTraits<DXContainerYAML::ResourceBindInfo> {
  /// Map PSV resource bind-info fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Res Resource bind info being mapped.
  LLVM_ABI static void mapping(IO &IO, DXContainerYAML::ResourceBindInfo &Res);
};

/// YAMLIO mapping traits for \c DXContainerYAML::SignatureElement.
template <> struct MappingTraits<DXContainerYAML::SignatureElement> {
  /// Map signature-element fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param El Signature element being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               llvm::DXContainerYAML::SignatureElement &El);
};

/// YAMLIO mapping traits for \c DXContainerYAML::StringTableEntry.
template <> struct MappingTraits<DXContainerYAML::StringTableEntry> {
  /// Map string-table entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E String-table entry being mapped.
  LLVM_ABI static void mapping(IO &IO, DXContainerYAML::StringTableEntry &E);
};

/// YAMLIO mapping traits for \c DXContainerYAML::SignatureParameter.
template <> struct MappingTraits<DXContainerYAML::SignatureParameter> {
  /// Map signature-parameter fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param El Signature parameter being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               llvm::DXContainerYAML::SignatureParameter &El);
};

/// YAMLIO mapping traits for \c DXContainerYAML::Signature.
template <> struct MappingTraits<DXContainerYAML::Signature> {
  /// Map signature fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param El Signature being mapped.
  LLVM_ABI static void mapping(IO &IO, llvm::DXContainerYAML::Signature &El);
};

/// YAMLIO mapping traits for \c DXContainerYAML::RootSignatureYamlDesc.
template <> struct MappingTraits<DXContainerYAML::RootSignatureYamlDesc> {
  /// Map root-signature fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param RootSignature Root signature being mapped.
  LLVM_ABI static void
  mapping(IO &IO, DXContainerYAML::RootSignatureYamlDesc &RootSignature);
};

/// YAMLIO context mapping traits for root-parameter locations.
template <>
struct MappingContextTraits<DXContainerYAML::RootParameterLocationYaml,
                            DXContainerYAML::RootSignatureYamlDesc> {
  /// Map a root-parameter location using the enclosing root signature.
  /// \param IO YAML input/output state.
  /// \param L Root-parameter location being mapped.
  /// \param S Enclosing root signature used as mapping context.
  LLVM_ABI static void
  mapping(IO &IO, llvm::DXContainerYAML::RootParameterLocationYaml &L,
          DXContainerYAML::RootSignatureYamlDesc &S);
};

/// YAMLIO mapping traits for \c DXContainerYAML::RootConstantsYaml.
template <> struct MappingTraits<llvm::DXContainerYAML::RootConstantsYaml> {
  /// Map root-constants fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param C Root constants being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               llvm::DXContainerYAML::RootConstantsYaml &C);
};

/// YAMLIO mapping traits for \c DXContainerYAML::RootDescriptorYaml.
template <> struct MappingTraits<llvm::DXContainerYAML::RootDescriptorYaml> {
  /// Map root-descriptor fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param D Root descriptor being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               llvm::DXContainerYAML::RootDescriptorYaml &D);
};

/// YAMLIO mapping traits for \c DXContainerYAML::DescriptorTableYaml.
template <> struct MappingTraits<llvm::DXContainerYAML::DescriptorTableYaml> {
  /// Map descriptor-table fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param D Descriptor table being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               llvm::DXContainerYAML::DescriptorTableYaml &D);
};

/// YAMLIO mapping traits for \c DXContainerYAML::DescriptorRangeYaml.
template <> struct MappingTraits<llvm::DXContainerYAML::DescriptorRangeYaml> {
  /// Map descriptor-range fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param D Descriptor range being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               llvm::DXContainerYAML::DescriptorRangeYaml &D);
};

/// YAMLIO mapping traits for \c DXContainerYAML::StaticSamplerYamlDesc.
template <>
struct MappingTraits<llvm::DXContainerYAML::StaticSamplerYamlDesc> {
  /// Map static-sampler fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param S Static sampler being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               llvm::DXContainerYAML::StaticSamplerYamlDesc &S);
};

/// YAMLIO mapping traits for \c DXContainerYAML::SourceInfo::Header.
template <> struct MappingTraits<llvm::DXContainerYAML::SourceInfo::Header> {
  /// Map source-info header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param H Source-info header being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               llvm::DXContainerYAML::SourceInfo::Header &H);
};

/// YAMLIO mapping traits for \c DXContainerYAML::SourceInfo::SectionHeader.
template <>
struct MappingTraits<llvm::DXContainerYAML::SourceInfo::SectionHeader> {
  /// Map source-info section-header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param H Section header being mapped.
  LLVM_ABI static void
  mapping(IO &IO, llvm::DXContainerYAML::SourceInfo::SectionHeader &H);
};

/// YAMLIO mapping traits for
/// \c DXContainerYAML::SourceInfo::SourceNames::Header.
template <>
struct MappingTraits<llvm::DXContainerYAML::SourceInfo::SourceNames::Header> {
  /// Map source-names header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param H Source-names header being mapped.
  LLVM_ABI static void
  mapping(IO &IO, llvm::DXContainerYAML::SourceInfo::SourceNames::Header &H);
};

/// YAMLIO mapping traits for
/// \c DXContainerYAML::SourceInfo::SourceNames::Entry.
template <>
struct MappingTraits<llvm::DXContainerYAML::SourceInfo::SourceNames::Entry> {
  /// Map source-name entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E Source-name entry being mapped.
  LLVM_ABI static void
  mapping(IO &IO, llvm::DXContainerYAML::SourceInfo::SourceNames::Entry &E);
};

/// YAMLIO mapping traits for \c DXContainerYAML::SourceInfo::SourceNames.
template <>
struct MappingTraits<llvm::DXContainerYAML::SourceInfo::SourceNames> {
  /// Map source-names section fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param S Source-names section being mapped.
  LLVM_ABI static void
  mapping(IO &IO, llvm::DXContainerYAML::SourceInfo::SourceNames &S);
};

/// YAMLIO mapping traits for
/// \c DXContainerYAML::SourceInfo::SourceContents::Header.
template <>
struct MappingTraits<
    llvm::DXContainerYAML::SourceInfo::SourceContents::Header> {
  /// Map source-contents header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param H Source-contents header being mapped.
  LLVM_ABI static void
  mapping(IO &IO, llvm::DXContainerYAML::SourceInfo::SourceContents::Header &H);
};

/// YAMLIO mapping traits for
/// \c DXContainerYAML::SourceInfo::SourceContents::Entry.
template <>
struct MappingTraits<llvm::DXContainerYAML::SourceInfo::SourceContents::Entry> {
  /// Map source-content entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E Source-content entry being mapped.
  LLVM_ABI static void
  mapping(IO &IO, llvm::DXContainerYAML::SourceInfo::SourceContents::Entry &E);
};

/// YAMLIO mapping traits for \c DXContainerYAML::SourceInfo::SourceContents.
template <>
struct MappingTraits<llvm::DXContainerYAML::SourceInfo::SourceContents> {
  /// Map source-contents section fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param S Source-contents section being mapped.
  LLVM_ABI static void
  mapping(IO &IO, llvm::DXContainerYAML::SourceInfo::SourceContents &S);
};

/// YAMLIO mapping traits for
/// \c DXContainerYAML::SourceInfo::ProgramArgs::Header.
template <>
struct MappingTraits<llvm::DXContainerYAML::SourceInfo::ProgramArgs::Header> {
  /// Map program-arguments header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param H Program-arguments header being mapped.
  LLVM_ABI static void
  mapping(IO &IO, llvm::DXContainerYAML::SourceInfo::ProgramArgs::Header &H);
};

/// YAMLIO mapping traits for \c mcdxbc::SourceInfo::ProgramArgs::Entry.
template <> struct MappingTraits<mcdxbc::SourceInfo::ProgramArgs::Entry> {
  /// Map program-argument entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E Program-argument entry being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               mcdxbc::SourceInfo::ProgramArgs::Entry &E);
};

/// YAMLIO mapping traits for \c DXContainerYAML::SourceInfo::ProgramArgs.
template <>
struct MappingTraits<llvm::DXContainerYAML::SourceInfo::ProgramArgs> {
  /// Map program-arguments section fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param S Program-arguments section being mapped.
  LLVM_ABI static void
  mapping(IO &IO, llvm::DXContainerYAML::SourceInfo::ProgramArgs &S);
};

/// YAMLIO mapping traits for \c DXContainerYAML::SourceInfo.
template <> struct MappingTraits<llvm::DXContainerYAML::SourceInfo> {
  /// Map source-info fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param S Source info being mapped.
  LLVM_ABI static void mapping(IO &IO, llvm::DXContainerYAML::SourceInfo &S);
};

} // namespace yaml

} // namespace llvm

#endif // LLVM_OBJECTYAML_DXCONTAINERYAML_H
