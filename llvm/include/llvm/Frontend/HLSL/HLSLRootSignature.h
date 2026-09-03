//===- HLSLRootSignature.h - HLSL Root Signature helper objects -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains structure definitions of HLSL Root Signature
/// objects.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_HLSL_HLSLROOTSIGNATURE_H
#define LLVM_FRONTEND_HLSL_HLSLROOTSIGNATURE_H

#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DXILABI.h"
#include "llvm/Support/raw_ostream.h"
#include <limits>
#include <variant>

namespace llvm {
namespace hlsl {
/// In-memory representations of HLSL root signature elements and helpers.
namespace rootsig {

// Definitions of the in-memory data layout structures

/// Kind of HLSL register: b (CBV), t (SRV), u (UAV), or s (Sampler).
enum class RegisterType {
  /// Constant buffer register (`b`).
  BReg,
  /// Shader resource view register (`t`).
  TReg,
  /// Unordered access view register (`u`).
  UReg,
  /// Sampler register (`s`).
  SReg
};

/// HLSL register binding such as \c b0, \c t1, \c u2, or \c s3.
struct Register {
  /// Register kind (b, t, u, or s).
  RegisterType ViewType;
  /// Register number within the view type.
  uint32_t Number;
};

/// Root constants parameter: a run of 32-bit values bound to a shader register.
struct RootConstants {
  /// Number of 32-bit values in the constants parameter.
  uint32_t Num32BitConstants;
  /// Shader register the constants are bound to.
  Register Reg;
  /// Register space of the constants.
  uint32_t Space = 0;
  /// Shader stages that can access these constants (\c ShaderVisibility).
  dxbc::ShaderVisibility Visibility = dxbc::ShaderVisibility::All;
};

/// Root CBV, SRV, or UAV descriptor parameter in a root signature.
struct RootDescriptor {
  /// Descriptor resource class (CBV, SRV, or UAV).
  dxil::ResourceClass Type;
  /// Shader register the descriptor is bound to.
  Register Reg;
  /// Register space of the descriptor.
  uint32_t Space = 0;
  /// Shader stages that can access this descriptor (\c ShaderVisibility).
  dxbc::ShaderVisibility Visibility = dxbc::ShaderVisibility::All;
  /// Root descriptor flags (\c RootDescriptorFlags).
  dxbc::RootDescriptorFlags Flags;

  /// Set \c Flags to the defaults for \p Version and this descriptor's type.
  ///
  /// \param Version Root signature version that selects the default flags.
  void setDefaultFlags(dxbc::RootSignatureVersion Version) {
    if (Version == dxbc::RootSignatureVersion::V1_0) {
      Flags = dxbc::RootDescriptorFlags::DataVolatile;
      return;
    }

    assert((Version == llvm::dxbc::RootSignatureVersion::V1_1 ||
            Version == llvm::dxbc::RootSignatureVersion::V1_2) &&
           "Specified an invalid root signature version");
    switch (Type) {
    case dxil::ResourceClass::CBuffer:
    case dxil::ResourceClass::SRV:
      Flags = dxbc::RootDescriptorFlags::DataStaticWhileSetAtExecute;
      break;
    case dxil::ResourceClass::UAV:
      Flags = dxbc::RootDescriptorFlags::DataVolatile;
      break;
    case dxil::ResourceClass::Sampler:
      llvm_unreachable(
          "ResourceClass::Sampler is not valid for RootDescriptors");
    }
  }
};

/// Descriptor table parameter; preceding clauses in the element array belong
/// to this table.
struct DescriptorTable {
  /// Shader stages that can access this table (\c ShaderVisibility).
  dxbc::ShaderVisibility Visibility = dxbc::ShaderVisibility::All;
  /// Number of preceding \c DescriptorTableClause elements that form this
  /// table.
  uint32_t NumClauses = 0;
};

static const uint32_t NumDescriptorsUnbounded = 0xffffffff;
static const uint32_t DescriptorTableOffsetAppend = 0xffffffff;

/// Descriptor table clause: a CBV, SRV, UAV, or Sampler range in a table.
struct DescriptorTableClause {
  /// Descriptor resource class (CBV, SRV, UAV, or Sampler).
  dxil::ResourceClass Type;
  /// First shader register in the range.
  Register Reg;
  /// Number of descriptors in the range, or \c NumDescriptorsUnbounded.
  uint32_t NumDescriptors = 1;
  /// Register space of the range.
  uint32_t Space = 0;
  /// Offset of this range from the start of the descriptor table, or
  /// \c DescriptorTableOffsetAppend.
  uint32_t Offset = DescriptorTableOffsetAppend;
  /// Descriptor range flags (\c DescriptorRangeFlags).
  dxbc::DescriptorRangeFlags Flags;

  /// Set \c Flags to the defaults for \p Version and this clause's type.
  ///
  /// \param Version Root signature version that selects the default flags.
  void setDefaultFlags(dxbc::RootSignatureVersion Version) {
    if (Version == dxbc::RootSignatureVersion::V1_0) {
      Flags = dxbc::DescriptorRangeFlags::DescriptorsVolatile;
      if (Type != dxil::ResourceClass::Sampler)
        Flags |= dxbc::DescriptorRangeFlags::DataVolatile;
      return;
    }

    assert((Version == dxbc::RootSignatureVersion::V1_1 ||
            Version == dxbc::RootSignatureVersion::V1_2) &&
           "Specified an invalid root signature version");
    switch (Type) {
    case dxil::ResourceClass::CBuffer:
    case dxil::ResourceClass::SRV:
      Flags = dxbc::DescriptorRangeFlags::DataStaticWhileSetAtExecute;
      break;
    case dxil::ResourceClass::UAV:
      Flags = dxbc::DescriptorRangeFlags::DataVolatile;
      break;
    case dxil::ResourceClass::Sampler:
      Flags = dxbc::DescriptorRangeFlags::None;
      break;
    }
  }
};

/// Static sampler descriptor in a root signature.
struct StaticSampler {
  /// Shader register the sampler is bound to.
  Register Reg;
  /// Filter used when sampling (\c SamplerFilter).
  dxbc::SamplerFilter Filter = dxbc::SamplerFilter::Anisotropic;
  /// Addressing mode for the U coordinate (\c TextureAddressMode).
  dxbc::TextureAddressMode AddressU = dxbc::TextureAddressMode::Wrap;
  /// Addressing mode for the V coordinate (\c TextureAddressMode).
  dxbc::TextureAddressMode AddressV = dxbc::TextureAddressMode::Wrap;
  /// Addressing mode for the W coordinate (\c TextureAddressMode).
  dxbc::TextureAddressMode AddressW = dxbc::TextureAddressMode::Wrap;
  /// Offset from the calculated mipmap level.
  float MipLODBias = 0.f;
  /// Clamping value for anisotropic filtering.
  uint32_t MaxAnisotropy = 16;
  /// Comparison function for comparison sampling (\c ComparisonFunc).
  dxbc::ComparisonFunc CompFunc = dxbc::ComparisonFunc::LessEqual;
  /// Border color for samples outside [0, 1] (\c StaticBorderColor).
  dxbc::StaticBorderColor BorderColor = dxbc::StaticBorderColor::OpaqueWhite;
  /// Lower end of the mipmap range to clamp access to.
  float MinLOD = 0.f;
  /// Upper end of the mipmap range to clamp access to.
  float MaxLOD = std::numeric_limits<float>::max();
  /// Register space of the sampler.
  uint32_t Space = 0;
  /// Shader stages that can access this sampler (\c ShaderVisibility).
  dxbc::ShaderVisibility Visibility = dxbc::ShaderVisibility::All;
  /// Static sampler flags (\c StaticSamplerFlags).
  dxbc::StaticSamplerFlags Flags = dxbc::StaticSamplerFlags::None;
};

/// Models RootElement : RootFlags | RootConstants | RootParam
///  | DescriptorTable | DescriptorTableClause | StaticSampler
///
/// A Root Signature is modeled in-memory by an array of RootElements. These
/// aim to map closely to their DSL grammar reprsentation defined in the spec.
///
/// Each optional parameter has its default value defined in the struct, and,
/// each mandatory parameter does not have a default initialization.
///
/// For the variants RootFlags, RootConstants, RootParam, StaticSampler and
/// DescriptorTableClause: each data member maps directly to a parameter in the
/// grammar.
///
/// The DescriptorTable is modelled by having its Clauses as the previous
/// RootElements in the array, and it holds a data member for the Visibility
/// parameter.
using RootElement =
    std::variant<dxbc::RootFlags, RootConstants, RootDescriptor,
                 DescriptorTable, DescriptorTableClause, StaticSampler>;

/// Write \p Flags to \p OS in a human-readable form.
///
/// \param OS Output stream.
/// \param Flags Root signature flags to print.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const dxbc::RootFlags &Flags);

/// Write \p Constants to \p OS in a human-readable form.
///
/// \param OS Output stream.
/// \param Constants Root constants parameter to print.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const RootConstants &Constants);

/// Write \p Clause to \p OS in a human-readable form.
///
/// \param OS Output stream.
/// \param Clause Descriptor table clause to print.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const DescriptorTableClause &Clause);

/// Write \p Table to \p OS in a human-readable form.
///
/// \param OS Output stream.
/// \param Table Descriptor table parameter to print.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const DescriptorTable &Table);

/// Write \p Descriptor to \p OS in a human-readable form.
///
/// \param OS Output stream.
/// \param Descriptor Root descriptor parameter to print.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const RootDescriptor &Descriptor);

/// Write \p StaticSampler to \p OS in a human-readable form.
///
/// \param OS Output stream.
/// \param StaticSampler Static sampler descriptor to print.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const StaticSampler &StaticSampler);

/// Write \p Element to \p OS by dispatching on its active variant.
///
/// \param OS Output stream.
/// \param Element Root signature element to print.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const RootElement &Element);

/// Dump each element of \p Elements to \p OS, one per line.
///
/// \param OS Output stream.
/// \param Elements Root signature elements to dump.
LLVM_ABI void dumpRootElements(raw_ostream &OS, ArrayRef<RootElement> Elements);

} // namespace rootsig
} // namespace hlsl
} // namespace llvm

#endif // LLVM_FRONTEND_HLSL_HLSLROOTSIGNATURE_H
