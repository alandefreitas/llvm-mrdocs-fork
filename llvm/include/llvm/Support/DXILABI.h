//===-- DXILABI.h - ABI Sensitive Values for DXIL ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions of various constants and enums that are
// required to remain stable as per the DXIL format's requirements.
//
// Documentation for DXIL can be found in
// https://github.com/Microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DXILABI_H
#define LLVM_SUPPORT_DXILABI_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm {
namespace dxil {

/// DXIL resource class identifying how a resource is bound and accessed.
enum class ResourceClass : uint8_t {
  SRV = 0,          ///< Shader resource view (read-only).
  UAV,              ///< Unordered access view.
  CBuffer,          ///< Constant buffer.
  Sampler,          ///< Sampler state.
  LastEntry = Sampler, ///< Sentinel equal to the last valid class.
};

/// Dimensionality of a DXIL resource.
enum class ResourceDimension : uint8_t {
  Unknown = 0, ///< Unknown or unspecified dimension.
  Dim1D,       ///< One-dimensional resource.
  Dim2D,       ///< Two-dimensional resource.
  Dim3D,       ///< Three-dimensional resource.
  Cube,        ///< Cube-map resource.
};

/// The kind of resource for an SRV or UAV resource. Sometimes referred to as
/// "Shape" in the DXIL docs.
enum class ResourceKind : uint32_t {
  Invalid = 0,             ///< Invalid or unspecified resource kind.
  Texture1D,               ///< One-dimensional texture.
  Texture2D,               ///< Two-dimensional texture.
  Texture2DMS,             ///< Multisampled two-dimensional texture.
  Texture3D,               ///< Three-dimensional texture.
  TextureCube,             ///< Cube-map texture.
  Texture1DArray,          ///< Array of one-dimensional textures.
  Texture2DArray,          ///< Array of two-dimensional textures.
  Texture2DMSArray,        ///< Array of multisampled two-dimensional textures.
  TextureCubeArray,        ///< Array of cube-map textures.
  TypedBuffer,             ///< Typed buffer.
  RawBuffer,               ///< Raw (byte-address) buffer.
  StructuredBuffer,        ///< Structured buffer.
  CBuffer,                 ///< Constant buffer.
  Sampler,                 ///< Sampler.
  TBuffer,                 ///< Texture buffer.
  RTAccelerationStructure, ///< Raytracing acceleration structure.
  FeedbackTexture2D,       ///< Two-dimensional sampler-feedback texture.
  FeedbackTexture2DArray,  ///< Array of two-dimensional sampler-feedback textures.
  NumEntries,              ///< Number of resource kind entries (sentinel).
};

/// The element type of an SRV or UAV resource.
enum class ElementType : uint32_t {
  Invalid = 0,   ///< Invalid or unspecified element type.
  I1,            ///< 1-bit integer.
  I16,           ///< 16-bit signed integer.
  U16,           ///< 16-bit unsigned integer.
  I32,           ///< 32-bit signed integer.
  U32,           ///< 32-bit unsigned integer.
  I64,           ///< 64-bit signed integer.
  U64,           ///< 64-bit unsigned integer.
  F16,           ///< 16-bit floating-point.
  F32,           ///< 32-bit floating-point.
  F64,           ///< 64-bit floating-point.
  SNormF16,      ///< 16-bit signed normalized floating-point.
  UNormF16,      ///< 16-bit unsigned normalized floating-point.
  SNormF32,      ///< 32-bit signed normalized floating-point.
  UNormF32,      ///< 32-bit unsigned normalized floating-point.
  SNormF64,      ///< 64-bit signed normalized floating-point.
  UNormF64,      ///< 64-bit unsigned normalized floating-point.
  PackedS8x32,   ///< Packed 32-bit value of signed 8-bit integers.
  PackedU8x32,   ///< Packed 32-bit value of unsigned 8-bit integers.
  LastEntry = PackedU8x32, ///< Sentinel equal to the last valid element type.
};

/// Metadata tags for extra resource properties.
enum class ExtPropTags : uint32_t {
  ElementType = 0,            ///< Element type of the resource.
  StructuredBufferStride = 1, ///< Stride of a structured buffer.
  SamplerFeedbackKind = 2,    ///< Kind of sampler feedback.
  Atomic64Use = 3,            ///< Whether the resource uses 64-bit atomics.
};

/// Type of a DXIL sampler.
enum class SamplerType : uint32_t {
  Default = 0,    ///< Default sampling.
  Comparison = 1, ///< Comparison sampling.
  Mono = 2,       ///< Mono sampling (seems to be unused).
};

/// Kind of sampler feedback reported by a feedback texture.
enum class SamplerFeedbackType : uint32_t {
  MinMip = 0,        ///< Minimum mip level accessed.
  MipRegionUsed = 1, ///< Mip region usage feedback.
};

/// Opcodes for the DXIL `AtomicBinOp` op (78). Values must match the DXIL
/// specification.
enum class AtomicBinOpCode : uint32_t {
  Add = 0,      ///< Atomic add.
  And = 1,      ///< Atomic bitwise AND.
  Or = 2,       ///< Atomic bitwise OR.
  Xor = 3,      ///< Atomic bitwise XOR.
  IMin = 4,     ///< Atomic signed minimum.
  IMax = 5,     ///< Atomic signed maximum.
  UMin = 6,     ///< Atomic unsigned minimum.
  UMax = 7,     ///< Atomic unsigned maximum.
  Exchange = 8, ///< Atomic exchange.
};

/// Minimum supported DXIL wave size.
const unsigned MinWaveSize = 4;
/// Maximum supported DXIL wave size.
const unsigned MaxWaveSize = 128;

/// Returns the name of a DXIL resource class.
///
/// \param RC Resource class to name.
/// \return The string name of \p RC.
LLVM_ABI StringRef getResourceClassName(ResourceClass RC);
} // namespace dxil
} // namespace llvm

#endif // LLVM_SUPPORT_DXILABI_H
