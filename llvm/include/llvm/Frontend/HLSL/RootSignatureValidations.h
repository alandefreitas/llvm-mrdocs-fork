//===- RootSignatureValidations.h - HLSL Root Signature helpers -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains helper obejcts for working with HLSL Root
/// Signatures.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_HLSL_ROOTSIGNATUREVALIDATIONS_H
#define LLVM_FRONTEND_HLSL_ROOTSIGNATUREVALIDATIONS_H

#include "llvm/ADT/IntervalMap.h"
#include "llvm/Frontend/HLSL/HLSLRootSignature.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace hlsl {
namespace rootsig {

// Basic verification of RootElements

/// Return true if \p Flags is a valid combination of root flags.
///
/// \param Flags Candidate root-flag bitfield.
/// \return \c true if \p Flags is a valid combination of root flags.
LLVM_ABI bool verifyRootFlag(uint32_t Flags);
/// Return true if \p Version is a supported root signature version.
///
/// \param Version Candidate root signature version (1, 2, or 3).
/// \return \c true if \p Version is a supported root signature version.
LLVM_ABI bool verifyVersion(uint32_t Version);
/// Return true if \p RegisterValue is a legal shader register index.
///
/// \param RegisterValue Candidate shader register number.
/// \return \c true if \p RegisterValue is a legal shader register index.
LLVM_ABI bool verifyRegisterValue(uint32_t RegisterValue);
/// Return true if \p RegisterSpace is a legal register space.
///
/// \param RegisterSpace Candidate register space number.
/// \return \c true if \p RegisterSpace is a legal register space.
LLVM_ABI bool verifyRegisterSpace(uint32_t RegisterSpace);
/// Return true if \p Flags are valid root-descriptor flags for \p Version.
///
/// \param Version Root signature version that constrains allowed flags.
/// \param Flags Candidate root-descriptor flags.
/// \return \c true if \p Flags are valid root-descriptor flags for \p Version.
LLVM_ABI bool verifyRootDescriptorFlag(uint32_t Version,
                                       dxbc::RootDescriptorFlags Flags);
/// Return true if \p Type is a valid descriptor-range resource class.
///
/// \param Type Candidate range type (\c dxil::ResourceClass).
/// \return \c true if \p Type is a valid descriptor-range resource class.
LLVM_ABI bool verifyRangeType(uint32_t Type);
/// Return true if \p Flags are valid descriptor-range flags for \p Version
/// and resource class \p Type.
///
/// \param Version Root signature version that constrains allowed flags.
/// \param Type Resource class of the descriptor range.
/// \param Flags Candidate descriptor-range flags.
/// \return \c true if \p Flags are valid descriptor-range flags for
///         \p Version and resource class \p Type.
LLVM_ABI bool verifyDescriptorRangeFlag(uint32_t Version,
                                        dxil::ResourceClass Type,
                                        dxbc::DescriptorRangeFlags Flags);
/// Return true if \p Flags are valid static-sampler flags for \p Version.
///
/// \param Version Root signature version that constrains allowed flags.
/// \param Flags Candidate static-sampler flags.
/// \return \c true if \p Flags are valid static-sampler flags for \p Version.
LLVM_ABI bool verifyStaticSamplerFlags(uint32_t Version,
                                       dxbc::StaticSamplerFlags Flags);
/// Return true if \p NumDescriptors is a positive descriptor-range size.
///
/// \param NumDescriptors Candidate number of descriptors in a range.
/// \return \c true if \p NumDescriptors is a positive descriptor-range size.
LLVM_ABI bool verifyNumDescriptors(uint32_t NumDescriptors);
/// Return true if \p MipLODBias is within the legal static-sampler range.
///
/// \param MipLODBias Candidate mip LOD bias value.
/// \return \c true if \p MipLODBias is within the legal static-sampler range.
LLVM_ABI bool verifyMipLODBias(float MipLODBias);
/// Return true if \p MaxAnisotropy is within the legal static-sampler range.
///
/// \param MaxAnisotropy Candidate maximum anisotropy value.
/// \return \c true if \p MaxAnisotropy is within the legal static-sampler range.
LLVM_ABI bool verifyMaxAnisotropy(uint32_t MaxAnisotropy);
/// Return true if \p LOD is a finite (non-NaN) level-of-detail value.
///
/// \param LOD Candidate minimum or maximum LOD value.
/// \return \c true if \p LOD is a finite (non-NaN) level-of-detail value.
LLVM_ABI bool verifyLOD(float LOD);

/// Return true if \p Offset fits in a 32-bit register offset.
///
/// \param Offset Candidate inclusive range bound or register offset.
/// \return \c true if \p Offset fits in a 32-bit register offset.
LLVM_ABI bool verifyNoOverflowedOffset(uint64_t Offset);
/// Compute the inclusive upper bound of a register range.
///
/// \param Offset Starting register index of the range.
/// \param Size Number of descriptors in the range, or
///        \c NumDescriptorsUnbounded.
/// \return Inclusive upper bound, or \c NumDescriptorsUnbounded when
///         \p Size is unbounded.
LLVM_ABI uint64_t computeRangeBound(uint64_t Offset, uint32_t Size);

} // namespace rootsig
} // namespace hlsl
} // namespace llvm

#endif // LLVM_FRONTEND_HLSL_ROOTSIGNATUREVALIDATIONS_H
