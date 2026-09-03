//===-- AMDGPUTargetParser - Parser for AMDGPU features ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise AMDGPU hardware features.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_AMDGPUTARGETPARSER_H
#define LLVM_TARGETPARSER_AMDGPUTARGETPARSER_H

#include "llvm/ADT/Bitset.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace llvm {

class raw_ostream;
template <typename T> class SmallVectorImpl;
class Triple;

namespace AMDGPU {

/// GPU kinds supported by the AMDGPU target.
enum GPUKind : uint8_t {
  /// Not specified processor.
  GK_NONE = 0,

#define GET_R600_GPU_ENUM
#include "llvm/TargetParser/R600TargetParserDef.inc"

#define GET_AMDGPU_GPU_ENUM
#include "llvm/TargetParser/AMDGPUTargetParserDef.inc"
};

/// One enumerator per frontend-visible feature bit; NUM_FEATURES is the count.
enum AMDGPUFeature : unsigned {
#define GET_AMDGPU_FEATURE_ENUM
#include "llvm/TargetParser/AMDGPUTargetParserDef.inc"
};

/// Bitset of enabled AMDGPU frontend-visible features.
using AMDGPUFeatureBitset = Bitset<NUM_FEATURES>;

/// Instruction set architecture version.
struct IsaVersion {
  /// Major ISA version component.
  uint8_t Major;
  /// Minor ISA version component.
  uint8_t Minor;
  /// Stepping ISA version component.
  uint8_t Stepping;

  /// True if this ISA version equals \p Other in all components.
  /// @param Other ISA version to compare against.
  /// @return True if this ISA version equals \p Other.
  bool operator==(const IsaVersion &Other) const {
    return Major == Other.Major && Minor == Other.Minor &&
           Stepping == Other.Stepping;
  }
  /// True if this ISA version differs from \p Other in any component.
  /// @param Other ISA version to compare against.
  /// @return True if this ISA version differs from \p Other.
  bool operator!=(const IsaVersion &Other) const { return !(*this == Other); }
};

/// R600 feature flags needed by the frontend driver.
///
/// This isn't comprehensive for now, just things that are needed from the
/// frontend driver.
enum R600FeatureKind : uint32_t {
  /// No R600 features.
  R600_FEATURE_NONE = 0,

  /// Has fma instructions.
  R600_FEATURE_FMA = 1 << 0,
};

/// GFX6+ architecture feature flags needed by the frontend driver.
///
/// This isn't comprehensive for now, just things that are needed from the
/// frontend driver.
enum ArchFeatureKind : uint32_t {
  /// No architecture features.
  FEATURE_NONE = 0,

  /// Fast FMA for f32.
  FEATURE_FAST_FMA_F32 = 1 << 0,
  /// Fast denormal handling for f32.
  FEATURE_FAST_DENORMAL_F32 = 1 << 1,

  /// Wavefront 32 is available.
  FEATURE_WAVE32 = 1 << 2,

  /// Xnack is available.
  FEATURE_XNACK = 1 << 3,

  /// Sram-ecc is available.
  FEATURE_SRAMECC = 1 << 4,

  /// WGP mode is supported.
  FEATURE_WGP = 1 << 5,

  /// Xnack on/off modes are supported.
  FEATURE_XNACK_ON_OFF_MODES = 1 << 6,

  /// VI SGPR initialization bug requiring a fixed SGPR allocation size.
  FEATURE_SGPR_INIT_BUG = 1 << 7
};

/// Result of validating AMDGPU target feature combinations.
enum FeatureError : uint32_t {
  /// No feature validation error.
  NO_ERROR = 0,
  /// Requested features are mutually incompatible.
  INVALID_FEATURE_COMBINATION,
  /// A requested feature is not supported by the target.
  UNSUPPORTED_TARGET_FEATURE
};

/// Returns the architecture family name for AMDGCN GPU kind \p AK.
/// @param AK GPU kind to query.
/// @return Architecture family name for \p AK.
LLVM_ABI StringRef getArchFamilyNameAMDGCN(GPUKind AK);

/// The canonical GPU name for a variant name.
/// @param AK GPU kind whose base/canonical name is requested.
/// @return Canonical/base GPU name for \p AK.
LLVM_ABI StringRef getBaseArchNameAMDGCN(GPUKind AK);

/// Returns the triple subarch for GPU kind \p AK.
/// @param AK GPU kind to query.
/// @return Triple subarch for \p AK.
LLVM_ABI Triple::SubArchType getSubArch(GPUKind AK);
/// Returns the major-family subarch for \p SubArch.
/// @param SubArch Sub-architecture whose major family is requested.
/// @return Major-family subarch for \p SubArch.
LLVM_ABI Triple::SubArchType getMajorSubArch(Triple::SubArchType SubArch);

/// Return true if the subarches of triples \p A and \p B are compatible.
///
/// They are equal or one is the major-family subarch of the other (e.g.
/// AMDGPUSubArch9 is compatible with AMDGPUSubArch900). NoSubArch is
/// compatible with anything.
/// @param A First triple whose subarch is compared.
/// @param B Second triple whose subarch is compared.
/// @return True if the subarches of \p A and \p B are compatible.
LLVM_ABI bool isSubArchCompatible(const Triple &A, const Triple &B);
/// Return true if subarch \p A is compatible with subarch \p B.
///
/// They are equal or one is the major-family subarch of the other (e.g.
/// AMDGPUSubArch9 is compatible with AMDGPUSubArch900). NoSubArch is
/// compatible with anything.
/// @param A First subarch to compare.
/// @param B Second subarch to compare.
/// @return True if \p A is compatible with \p B.
LLVM_ABI bool isSubArchCompatible(Triple::SubArchType A, Triple::SubArchType B);

/// Return true if GPU \p AK is usable with triple subarch \p SubArch.
///
/// A NoSubArch triple (legacy "amdgcn") accepts any GPU. Otherwise the GPU's
/// subarch must equal \p SubArch, or \p SubArch must be the major-family
/// subarch of the GPU (e.g. the amdgpu9 triple accepts gfx900).
/// @param SubArch Triple subarch to validate against.
/// @param AK GPU kind to check.
/// @return True if \p AK is valid for \p SubArch.
LLVM_ABI bool isCPUValidForSubArch(Triple::SubArchType SubArch, GPUKind AK);

/// Return true if GPU name \p CPU is usable with triple subarch \p SubArch.
///
/// Convenience overload of isCPUValidForSubArch taking a GPU name \p CPU, which
/// is parsed via parseArchAMDGCN. An unrecognized name is never valid.
/// @param SubArch Triple subarch to validate against.
/// @param CPU GPU name to parse and check.
/// @return True if \p CPU is valid for \p SubArch.
LLVM_ABI bool isCPUValidForSubArch(Triple::SubArchType SubArch, StringRef CPU);

/// Return true if \p AK is a pseudo target such as "generic" or "generic-hsa".
///
/// A recognized AMDGCN GPU that represents no concrete hardware and has no
/// subarch of its own. Such targets are resolved by the backend as a default
/// device but are not valid as an explicit -mcpu.
/// @param AK GPU kind to check.
/// @return True if \p AK is a pseudo target.
LLVM_ABI bool isPseudoTarget(GPUKind AK);

/// Return true if GPU name \p CPU is a pseudo target.
///
/// Convenience overload of isPseudoTarget taking a GPU name \p CPU, which is
/// parsed via parseArchAMDGCN.
/// @param CPU GPU name to parse and check.
/// @return True if \p CPU is a pseudo target.
LLVM_ABI bool isPseudoTarget(StringRef CPU);

/// Returns the effective triple appropriate to use when linking \p B into \p A
/// by merging the subarches in case of inexact match.
///
/// In cases where isSubArchCompatible would return false, returns \p B. This
/// assumes that the non-arch triple components are the same.
/// @param A Destination/link-into triple.
/// @param B Source/linked-from triple.
/// @return Effective triple string after merging the subarches.
LLVM_ABI std::string mergeSubArch(const Triple &A, const Triple &B);

/// Returns the canonical AMDGCN architecture name for GPU kind \p AK.
/// @param AK GPU kind to query.
/// @return Canonical AMDGCN architecture name for \p AK.
LLVM_ABI StringRef getArchNameAMDGCN(GPUKind AK);
/// Returns the canonical R600 architecture name for GPU kind \p AK.
/// @param AK GPU kind to query.
/// @return Canonical R600 architecture name for \p AK.
LLVM_ABI StringRef getArchNameR600(GPUKind AK);

/// Returns the canonical GPU name for an AMDGPU subarch.
///
/// For example, AMDGPUSubArch1030 -> "gfx1030", AMDGPUSubArch9 ->
/// "gfx9-generic", AMDGPUSubArch6 -> "gfx600". Returns "" for NoSubArch or a
/// non-AMDGPU subarch. The major-only subarches map to their generic/lowest
/// representative, matching the default subtarget for an unspecified -mcpu.
/// @param SubArch Sub-architecture to map to a GPU name.
/// @return Canonical GPU name, or empty for NoSubArch or a non-AMDGPU subarch.
LLVM_ABI StringRef getArchNameFromSubArch(Triple::SubArchType SubArch);

/// Returns the triple subarch name for an AMDGPU subarch, e.g.
/// AMDGPUSubArch900 -> "amdgpu9.00". Returns "amdgpu" for NoSubArch.
/// @param SubArch Sub-architecture whose triple name is requested.
/// @return Triple subarch name string for \p SubArch.
LLVM_ABI StringRef getSubArchName(Triple::SubArchType SubArch);
/// Returns the canonical architecture name for \p Arch under triple \p T.
/// @param T Target triple that selects the architecture family.
/// @param Arch Architecture name to canonicalize.
/// @return Canonical architecture name for \p Arch under \p T.
LLVM_ABI StringRef getCanonicalArchName(const Triple &T, StringRef Arch);
/// Parses an AMDGCN GPU name \p CPU into a GPUKind.
/// @param CPU AMDGCN GPU name string.
/// @return Parsed GPUKind for \p CPU.
LLVM_ABI GPUKind parseArchAMDGCN(StringRef CPU);
/// Parses an R600 GPU name \p CPU into a GPUKind.
/// @param CPU R600 GPU name string.
/// @return Parsed GPUKind for \p CPU.
LLVM_ABI GPUKind parseArchR600(StringRef CPU);
/// Returns the representative GPUKind for subarch \p SubArch.
/// @param SubArch Sub-architecture to map to a GPU kind.
/// @return Representative GPUKind for \p SubArch.
LLVM_ABI GPUKind getGPUKindFromSubArch(Triple::SubArchType SubArch);
/// Returns the legacy ArchFeatureKind bitfield for AMDGCN GPU kind \p AK.
///
/// @deprecated Use getFeatureBitset and test the relevant FEAT_* bits instead.
/// The legacy ArchFeatureKind bitfield is being removed.
/// @param AK GPU kind to query.
/// @return Legacy ArchFeatureKind bitfield for \p AK.
LLVM_DEPRECATED("use getFeatureBitset instead", "getFeatureBitset")
LLVM_ABI unsigned getArchAttrAMDGCN(GPUKind AK);
/// Returns the legacy ArchFeatureKind bitfield for AMDGCN subarch \p SubArch.
///
/// @deprecated Use getFeatureBitset and test the relevant FEAT_* bits instead.
/// The legacy ArchFeatureKind bitfield is being removed.
/// @param SubArch Sub-architecture to query.
/// @return Legacy ArchFeatureKind bitfield for \p SubArch.
LLVM_DEPRECATED("use getFeatureBitset instead", "getFeatureBitset")
LLVM_ABI unsigned getArchAttrAMDGCN(Triple::SubArchType SubArch);
/// Returns the R600FeatureKind bitfield for GPU kind \p AK.
/// @param AK GPU kind to query.
/// @return R600FeatureKind bitfield for \p AK.
LLVM_ABI R600FeatureKind getArchAttrR600(GPUKind AK);

/// Returns \p AK's feature bitset, or an empty bitset if unknown.
/// @param AK GPU kind whose features are requested.
/// @return Feature bitset for \p AK, or an empty bitset if unknown.
LLVM_ABI const AMDGPUFeatureBitset &getFeatureBitset(GPUKind AK);

/// Appends the feature name of each bit set in \p Features to \p Names.
/// @param Features Feature bitset to expand into names.
/// @param Names Destination list that receives feature name strings.
LLVM_ABI void getFeatureNames(const AMDGPUFeatureBitset &Features,
                              SmallVectorImpl<StringRef> &Names);

/// Append the valid AMDGCN GPU names to \p Values.
///
/// If \p SubArch is not NoSubArch, only GPUs compatible with that subarch (see
/// isCPUValidForSubArch) are appended.
/// @param Values Destination list that receives valid GPU names.
/// @param SubArch Optional subarch filter; NoSubArch accepts all AMDGCN GPUs.
LLVM_ABI void
fillValidArchListAMDGCN(SmallVectorImpl<StringRef> &Values,
                        Triple::SubArchType SubArch = Triple::NoSubArch);
/// Append the valid R600 GPU names to \p Values.
/// @param Values Destination list that receives valid GPU names.
LLVM_ABI void fillValidArchListR600(SmallVectorImpl<StringRef> &Values);

/// Returns the ISA version for GPU name \p GPU.
/// @param GPU GPU name string.
/// @return ISA version for \p GPU.
LLVM_ABI IsaVersion getIsaVersion(StringRef GPU);
/// Returns the ISA version for subarch \p SubArch.
/// @param SubArch Sub-architecture to query.
/// @return ISA version for \p SubArch.
LLVM_ABI IsaVersion getIsaVersion(Triple::SubArchType SubArch);

/// Constants for GPUs affected by the VI SGPR initialization bug.
enum {
  /// Fixed SGPR allocation size used when FEATURE_SGPR_INIT_BUG applies.
  FIXED_NUM_SGPRS_FOR_INIT_BUG = 96
};

/// Returns the total number of SGPRs for GPU kind \p AK.
/// @param AK GPU kind to query.
/// @return Total number of SGPRs for \p AK.
LLVM_ABI unsigned getTotalNumSGPRs(GPUKind AK);
/// Returns the total number of SGPRs for subarch \p SubArch.
/// @param SubArch Sub-architecture to query.
/// @return Total number of SGPRs for \p SubArch.
LLVM_ABI unsigned getTotalNumSGPRs(Triple::SubArchType SubArch);

/// Returns the number of addressable SGPRs for GPU kind \p AK.
/// @param AK GPU kind to query.
/// @return Number of addressable SGPRs for \p AK.
LLVM_ABI unsigned getAddressableNumSGPRs(GPUKind AK);
/// Returns the number of addressable SGPRs for subarch \p SubArch.
/// @param SubArch Sub-architecture to query.
/// @return Number of addressable SGPRs for \p SubArch.
LLVM_ABI unsigned getAddressableNumSGPRs(Triple::SubArchType SubArch);

/// Returns the SGPR allocation granule for GPU kind \p AK.
/// @param AK GPU kind to query.
/// @return SGPR allocation granule for \p AK.
LLVM_ABI unsigned getSGPRAllocGranule(GPUKind AK);
/// Returns the SGPR allocation granule for subarch \p SubArch.
/// @param SubArch Sub-architecture to query.
/// @return SGPR allocation granule for \p SubArch.
LLVM_ABI unsigned getSGPRAllocGranule(Triple::SubArchType SubArch);

/// Maximum LDS in bytes a single work-group can address.
///
/// This is a fixed hardware cap and does not depend on how many SIMDs a
/// work-group runs on.
/// @param AK GPU kind to query.
/// @return Maximum addressable LDS size in bytes for \p AK.
LLVM_ABI unsigned getMaxHWAddressableLocalMemorySize(GPUKind AK);
/// Maximum LDS in bytes a single work-group can address for \p SubArch.
///
/// This is a fixed hardware cap and does not depend on how many SIMDs a
/// work-group runs on.
/// @param SubArch Sub-architecture to query.
/// @return Maximum addressable LDS size in bytes for \p SubArch.
LLVM_ABI unsigned
getMaxHWAddressableLocalMemorySize(Triple::SubArchType SubArch);

/// Number of SIMDs a work-group's waves run on.
///
/// All four SIMDs of the functional block in full-SIMD mode, half of them
/// otherwise.
/// @param FullSIMDMode Whether the work-group uses the full SIMD set.
/// @return Number of SIMDs used by the work-group.
constexpr unsigned getNumWorkGroupSIMDs(bool FullSIMDMode) {
  return FullSIMDMode ? 4 : 2;
}

/// Minimum number of waves per execution unit.
/// @return Minimum waves per execution unit.
constexpr unsigned getMinWavesPerEU() { return 1; }

/// Maximum number of waves per execution unit without any kind of limitation.
/// @param AK GPU kind to query.
/// @return Maximum waves per execution unit for \p AK.
LLVM_ABI unsigned getMaxWavesPerEU(GPUKind AK);
/// Maximum number of waves per execution unit without any kind of limitation.
/// @param SubArch Sub-architecture to query.
/// @return Maximum waves per execution unit for \p SubArch.
LLVM_ABI unsigned getMaxWavesPerEU(Triple::SubArchType SubArch);

/// Fills \p Features with default values for the given target GPU.
///
/// \p Features contains overriding target features and this function returns
/// default target features with entries overridden by \p Features.
/// @param GPU Target GPU name.
/// @param T Target triple.
/// @param Features In/out map of feature overrides; updated with defaults.
/// @return Pair of FeatureError and a StringRef naming the offending feature.
LLVM_ABI std::pair<FeatureError, StringRef>
fillAMDGPUFeatureMap(StringRef GPU, const Triple &T, StringMap<bool> &Features);

/// Setting of an optional TargetID feature such as xnack or sramecc.
enum class TargetIDSetting {
  /// Feature is not supported by the target.
  Unsupported,
  /// Feature may be either on or off.
  Any,
  /// Feature is explicitly disabled.
  Off,
  /// Feature is explicitly enabled.
  On
};

/// AMDGPU target identity: processor plus optional xnack/sramecc settings.
class LLVM_ABI TargetID {
private:
  GPUKind Arch;
  std::string TargetTripleString;
  TargetIDSetting XnackSetting;
  TargetIDSetting SramEccSetting;
  bool IsAMDHSA;

public:
  /// Construct a TargetID from GPU kind, triple, and feature settings.
  /// @param Arch GPU kind / processor.
  /// @param TT Target triple.
  /// @param XnackSetting Initial xnack setting.
  /// @param SramEccSetting Initial sramecc setting.
  TargetID(GPUKind Arch, const Triple &TT, TargetIDSetting XnackSetting,
           TargetIDSetting SramEccSetting);

  /// Construct a TargetID from a triple and processor+features string.
  ///
  /// Examples of \p TargetIDStr: "gfx90a", "gfx90a:xnack+:sramecc-", "".
  /// @param TT Target triple.
  /// @param TargetIDStr Processor name with optional feature modifiers.
  TargetID(const Triple &TT, StringRef TargetIDStr);

  /// Destroy this TargetID.
  ~TargetID() = default;

  /// Returns true if the current xnack setting is not "Unsupported".
  /// @return True if xnack is supported.
  bool isXnackSupported() const {
    return XnackSetting != TargetIDSetting::Unsupported;
  }

  /// Returns true if the current xnack setting is "On" or "Any".
  /// @return True if xnack is On or Any.
  bool isXnackOnOrAny() const {
    return XnackSetting == TargetIDSetting::On ||
           XnackSetting == TargetIDSetting::Any;
  }

  /// Returns true if the current xnack setting is "On" or "Off".
  /// @return True if xnack is On or Off.
  bool isXnackOnOrOff() const {
    return getXnackSetting() == TargetIDSetting::On ||
           getXnackSetting() == TargetIDSetting::Off;
  }

  /// Returns the current xnack TargetIDSetting.
  ///
  /// Possible options are "Unsupported", "Any", "Off", and "On".
  /// @return Current xnack setting.
  TargetIDSetting getXnackSetting() const { return XnackSetting; }

  /// Sets xnack setting to \p NewXnackSetting.
  /// @param NewXnackSetting New xnack setting to apply.
  void setXnackSetting(TargetIDSetting NewXnackSetting) {
    XnackSetting = NewXnackSetting;
  }

  /// Returns true if the current sramecc setting is not "Unsupported".
  /// @return True if sramecc is supported.
  bool isSramEccSupported() const {
    return SramEccSetting != TargetIDSetting::Unsupported;
  }

  /// Returns true if the current sramecc setting is "On" or "Any".
  /// @return True if sramecc is On or Any.
  bool isSramEccOnOrAny() const {
    return SramEccSetting == TargetIDSetting::On ||
           SramEccSetting == TargetIDSetting::Any;
  }

  /// Returns true if the current sramecc setting is "On" or "Off".
  /// @return True if sramecc is On or Off.
  bool isSramEccOnOrOff() const {
    return getSramEccSetting() == TargetIDSetting::On ||
           getSramEccSetting() == TargetIDSetting::Off;
  }

  /// Returns the current sramecc TargetIDSetting.
  ///
  /// Possible options are "Unsupported", "Any", "Off", and "On".
  /// @return Current sramecc setting.
  TargetIDSetting getSramEccSetting() const { return SramEccSetting; }

  /// Sets sramecc setting to \p NewSramEccSetting.
  /// @param NewSramEccSetting New sramecc setting to apply.
  void setSramEccSetting(TargetIDSetting NewSramEccSetting) {
    SramEccSetting = NewSramEccSetting;
  }

  /// Returns the GPU kind / processor for this TargetID.
  /// @return GPU kind for this TargetID.
  GPUKind getGPUKind() const { return Arch; }

  /// Returns the target triple string for this TargetID.
  /// @return Target triple string.
  StringRef getTargetTripleString() const { return TargetTripleString; }

  /// Returns true if this is an AMDHSA target.
  /// @return True if this is an AMDHSA target.
  bool isAMDHSA() const { return IsAMDHSA; }

  /// Parse and validate a TargetID for triple \p TT.
  ///
  /// The processor+features string \p ProcAndFeatures may look like "gfx90a",
  /// "gfx90a:xnack+:sramecc-", or "". Returns std::nullopt if the triple is not
  /// AMDGCN, the processor is unrecognized, or a feature modifier is invalid
  /// for the processor.
  /// @param TT Target triple that must be AMDGCN.
  /// @param ProcAndFeatures Processor name with optional feature modifiers.
  /// @return Parsed TargetID, or std::nullopt on validation failure.
  static std::optional<TargetID> parse(const Triple &TT,
                                       StringRef ProcAndFeatures);

  /// Parse and validate a TargetID from a full directive string.
  ///
  /// Expects a "<triple>-<processor>:<features>" directive string.
  /// @param TargetIDDirective Full target-id directive to parse.
  /// @return Parsed TargetID, or std::nullopt on validation failure.
  static std::optional<TargetID>
  parseTargetIDString(StringRef TargetIDDirective);

  /// Returns true if \p Other denotes the same target as *this.
  ///
  /// Same processor and xnack/sramecc settings on a compatible triple. This is
  /// a semantic equality that looks through spelling differences.
  /// @param Other TargetID to compare against.
  /// @return True if \p Other denotes the same target as *this.
  bool isEquivalent(const TargetID &Other) const;

  /// Returns true if a device image for *this can provide code for \p Other.
  ///
  /// This is directional and models logical-linking compatibility.
  /// @param Other Requested TargetID that must be satisfiable by *this.
  /// @return True if *this can provide code for \p Other.
  bool providesFor(const TargetID &Other) const;

  /// Prints this TargetID to \p OS.
  /// @param OS Output stream.
  void print(raw_ostream &OS) const;

  /// Returns this TargetID as a string.
  /// @return String representation of this TargetID.
  std::string toString() const;

  /// Print the canonical processor name and explicit feature modifiers.
  ///
  /// Writes e.g. "gfx908:sramecc-:xnack+" without the triple prefix.
  /// @param OS Output stream.
  void printCanonicalTargetIDString(raw_ostream &OS) const;

  /// Returns the canonical processor name and explicit feature modifiers.
  ///
  /// For example "gfx908:sramecc-:xnack+", without the triple prefix.
  /// @return Canonical processor and feature string without the triple prefix.
  std::string getCanonicalFeatureString() const;

  /// True if this TargetID equals \p Other.
  /// @param Other TargetID to compare against.
  /// @return True if this TargetID equals \p Other.
  bool operator==(const TargetID &Other) const;
  /// True if this TargetID is not equal to \p Other.
  /// @param Other TargetID to compare against.
  /// @return True if this TargetID is not equal to \p Other.
  bool operator!=(const TargetID &Other) const { return !(*this == Other); }
};

/// Writes \p TargetID to \p OS.
/// @param OS Output stream.
/// @param TargetID Target identity to print.
/// @return \p OS after writing \p TargetID.
inline raw_ostream &operator<<(raw_ostream &OS, const TargetID &TargetID) {
  TargetID.print(OS);
  return OS;
}

} // namespace AMDGPU

} // namespace llvm

#endif
