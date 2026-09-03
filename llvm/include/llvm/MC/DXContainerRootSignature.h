//===- llvm/MC/DXContainerRootSignature.h - RootSignature -*- C++ -*- ========//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_DXCONTAINERROOTSIGNATURE_H
#define LLVM_MC_DXCONTAINERROOTSIGNATURE_H

#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <limits>

namespace llvm {

class raw_ostream;
namespace mcdxbc {

/// Root constants parameter: a run of 32-bit values bound to a shader register.
struct RootConstants {
  /// Shader register the constants are bound to.
  uint32_t ShaderRegister;
  /// Register space of the constants.
  uint32_t RegisterSpace;
  /// Number of 32-bit values in the constants parameter.
  uint32_t Num32BitValues;
};

/// Root CBV, SRV, or UAV descriptor in a root signature.
struct RootDescriptor {
  /// Shader register the descriptor is bound to.
  uint32_t ShaderRegister;
  /// Register space of the descriptor.
  uint32_t RegisterSpace;
  /// Root descriptor flags (\c RootDescriptorFlags).
  uint32_t Flags;
};

/// Descriptor range within a descriptor table.
struct DescriptorRange {
  /// Descriptor range type (\c dxil::ResourceClass).
  dxil::ResourceClass RangeType;
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
};

/// Metadata for one root parameter and its location in a type-specific store.
struct RootParameterInfo {
  /// Parameter kind (\c RootParameterType).
  dxbc::RootParameterType Type;
  /// Shader stages that can access this parameter (\c ShaderVisibility).
  dxbc::ShaderVisibility Visibility;
  /// Index into the constants, descriptors, or tables vector for this type.
  size_t Location;

  /// Construct root-parameter metadata from a type, visibility, and location.
  ///
  /// \param Type - Parameter kind.
  /// \param Visibility - Shader stages that can access the parameter.
  /// \param Location - Index into the matching type-specific storage vector.
  RootParameterInfo(dxbc::RootParameterType Type,
                    dxbc::ShaderVisibility Visibility, size_t Location)
      : Type(Type), Visibility(Visibility), Location(Location) {}
};

/// Descriptor table containing one or more descriptor ranges.
struct DescriptorTable {
  /// Ranges that make up this descriptor table.
  SmallVector<DescriptorRange> Ranges;
  /// Return an iterator to the first range in the table.
  ///
  /// \return Iterator to the first range in the table.
  SmallVector<DescriptorRange>::const_iterator begin() const {
    return Ranges.begin();
  }
  /// Return an iterator past the last range in the table.
  ///
  /// \return Iterator past the last range in the table.
  SmallVector<DescriptorRange>::const_iterator end() const {
    return Ranges.end();
  }
};

/// Static sampler descriptor in a root signature.
struct StaticSampler {
  /// Filter used when sampling (\c SamplerFilter).
  dxbc::SamplerFilter Filter;
  /// Addressing mode for the U coordinate (\c TextureAddressMode).
  dxbc::TextureAddressMode AddressU;
  /// Addressing mode for the V coordinate (\c TextureAddressMode).
  dxbc::TextureAddressMode AddressV;
  /// Addressing mode for the W coordinate (\c TextureAddressMode).
  dxbc::TextureAddressMode AddressW;
  /// Offset from the calculated mipmap level.
  float MipLODBias;
  /// Clamping value for anisotropic filtering.
  uint32_t MaxAnisotropy;
  /// Comparison function for comparison sampling (\c ComparisonFunc).
  dxbc::ComparisonFunc ComparisonFunc;
  /// Border color for samples outside [0, 1] (\c StaticBorderColor).
  dxbc::StaticBorderColor BorderColor;
  /// Lower end of the mipmap range to clamp access to.
  float MinLOD;
  /// Upper end of the mipmap range to clamp access to.
  float MaxLOD;
  /// Shader register the sampler is bound to.
  uint32_t ShaderRegister;
  /// Register space of the sampler.
  uint32_t RegisterSpace;
  /// Shader stages that can access this sampler (\c ShaderVisibility).
  dxbc::ShaderVisibility ShaderVisibility;
  /// Static sampler flags (\c StaticSamplerFlags); meaningful from version 1.2
  /// onward.
  uint32_t Flags = 0;
};

/// In-memory store of root parameters keyed by type-specific locations.
struct RootParametersContainer {
  /// Ordered metadata for each root parameter in the signature.
  SmallVector<RootParameterInfo> ParametersInfo;

  /// Root-constants payloads referenced by constant parameters.
  SmallVector<RootConstants> Constants;
  /// Root-descriptor payloads referenced by CBV, SRV, or UAV parameters.
  SmallVector<RootDescriptor> Descriptors;
  /// Descriptor-table payloads referenced by table parameters.
  SmallVector<DescriptorTable> Tables;

  /// Append root-parameter metadata without storing a payload.
  ///
  /// \param Type - Parameter kind.
  /// \param Visibility - Shader stages that can access the parameter.
  /// \param Location - Index into the matching type-specific storage vector.
  void addInfo(dxbc::RootParameterType Type, dxbc::ShaderVisibility Visibility,
               size_t Location) {
    ParametersInfo.emplace_back(Type, Visibility, Location);
  }

  /// Append a root-constants parameter and its payload.
  ///
  /// \param Type - Parameter kind (typically constants).
  /// \param Visibility - Shader stages that can access the parameter.
  /// \param Constant - Root-constants payload to store.
  void addParameter(dxbc::RootParameterType Type,
                    dxbc::ShaderVisibility Visibility, RootConstants Constant) {
    addInfo(Type, Visibility, Constants.size());
    Constants.push_back(Constant);
  }

  /// Append a root-descriptor parameter and its payload.
  ///
  /// \param Type - Parameter kind (CBV, SRV, or UAV).
  /// \param Visibility - Shader stages that can access the parameter.
  /// \param Descriptor - Root-descriptor payload to store.
  void addParameter(dxbc::RootParameterType Type,
                    dxbc::ShaderVisibility Visibility,
                    RootDescriptor Descriptor) {
    addInfo(Type, Visibility, Descriptors.size());
    Descriptors.push_back(Descriptor);
  }

  /// Append a descriptor-table parameter and its payload.
  ///
  /// \param Type - Parameter kind (typically a descriptor table).
  /// \param Visibility - Shader stages that can access the parameter.
  /// \param Table - Descriptor-table payload to store.
  void addParameter(dxbc::RootParameterType Type,
                    dxbc::ShaderVisibility Visibility, DescriptorTable Table) {
    addInfo(Type, Visibility, Tables.size());
    Tables.push_back(Table);
  }

  /// Return the root-parameter metadata at \p Location.
  ///
  /// \param Location - Index into \c ParametersInfo.
  /// \return Root-parameter metadata at \p Location.
  const RootParameterInfo &getInfo(uint32_t Location) const {
    const RootParameterInfo &Info = ParametersInfo[Location];
    return Info;
  }

  /// Return the root-constants payload at \p Index.
  ///
  /// \param Index - Index into \c Constants.
  /// \return Root-constants payload at \p Index.
  const RootConstants &getConstant(size_t Index) const {
    return Constants[Index];
  }

  /// Return the root-descriptor payload at \p Index.
  ///
  /// \param Index - Index into \c Descriptors.
  /// \return Root-descriptor payload at \p Index.
  const RootDescriptor &getRootDescriptor(size_t Index) const {
    return Descriptors[Index];
  }

  /// Return the descriptor-table payload at \p Index.
  ///
  /// \param Index - Index into \c Tables.
  /// \return Descriptor-table payload at \p Index.
  const DescriptorTable &getDescriptorTable(size_t Index) const {
    return Tables[Index];
  }

  /// Return the number of root parameters in the container.
  ///
  /// \return Number of root parameters in the container.
  size_t size() const { return ParametersInfo.size(); }

  /// Return an iterator to the first root-parameter metadata entry.
  ///
  /// \return Iterator to the first root-parameter metadata entry.
  SmallVector<RootParameterInfo>::const_iterator begin() const {
    return ParametersInfo.begin();
  }
  /// Return an iterator past the last root-parameter metadata entry.
  ///
  /// \return Iterator past the last root-parameter metadata entry.
  SmallVector<RootParameterInfo>::const_iterator end() const {
    return ParametersInfo.end();
  }
};

/// In-memory representation of a DXContainer root signature (RTS0) part.
struct RootSignatureDesc {

  /// Serialized root signature version (\c RootSignatureVersion).
  uint32_t Version = 2U;
  /// Root signature flags (\c RootFlags).
  uint32_t Flags = 0U;
  /// Offset from the start of the RTS0 part to the parameter header array.
  uint32_t RootParameterOffset = 0U;
  /// Offset from the start of the RTS0 part to the static sampler array.
  uint32_t StaticSamplersOffset = 0u;
  /// Number of static samplers in the signature.
  uint32_t NumStaticSamplers = 0u;
  /// Root parameters that make up the signature body.
  mcdxbc::RootParametersContainer ParametersContainer;
  /// Static samplers appended after the root parameters.
  SmallVector<StaticSampler> StaticSamplers;

  /// Serialize the root signature to \p OS.
  ///
  /// \param OS - Stream to write the RTS0 part to.
  LLVM_ABI void write(raw_ostream &OS) const;

  /// Return the serialized size in bytes of this root signature.
  ///
  /// \return Serialized size in bytes of this root signature.
  LLVM_ABI size_t getSize() const;
  /// Return the byte offset of the root-parameter array within the RTS0 part.
  ///
  /// \return Byte offset of the root-parameter array within the RTS0 part.
  LLVM_ABI uint32_t computeRootParametersOffset() const;
  /// Return the byte offset of the static-sampler array within the RTS0 part.
  ///
  /// \return Byte offset of the static-sampler array within the RTS0 part.
  LLVM_ABI uint32_t computeStaticSamplersOffset() const;
};
} // namespace mcdxbc
} // namespace llvm

#endif // LLVM_MC_DXCONTAINERROOTSIGNATURE_H
