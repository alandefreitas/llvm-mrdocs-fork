//===-- RISCVISAInfo.h - RISC-V ISA Information -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RISCVISAINFO_H
#define LLVM_SUPPORT_RISCVISAINFO_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/RISCVISAUtils.h"

#include <set>
#include <string>
#include <vector>

namespace llvm {

/// Parsed RISC-V ISA extension set and derived length properties.
class RISCVISAInfo {
public:
  /// Copy construction is deleted; instances are created via factory methods.
  ///
  /// \param Other Unused; copy construction is deleted.
  RISCVISAInfo(const RISCVISAInfo &Other) = delete;
  /// Copy assignment is deleted; instances are created via factory methods.
  ///
  /// \param Other Unused; copy assignment is deleted.
  RISCVISAInfo &operator=(const RISCVISAInfo &Other) = delete;

  /// Parse RISC-V ISA info from an architecture string.
  ///
  /// Unrecognized extension names or versions are reported as errors.
  /// Experimental extensions and profiles require
  /// \p EnableExperimentalExtension; when
  /// \p ExperimentalExtensionVersionCheck is set, experimental extension
  /// versions are validated.
  ///
  /// \param Arch Architecture string (e.g. "rv64gc") or profile name.
  /// \param EnableExperimentalExtension Whether experimental extensions and
  ///        profiles are allowed.
  /// \param ExperimentalExtensionVersionCheck Whether to validate versions of
  ///        experimental extensions.
  /// \returns Parsed ISA info, or an error.
  LLVM_ABI static llvm::Expected<std::unique_ptr<RISCVISAInfo>>
  parseArchString(StringRef Arch, bool EnableExperimentalExtension,
                  bool ExperimentalExtensionVersionCheck = true);

  /// Parse RISC-V ISA info from a normalized architecture string.
  ///
  /// The string must already be in normalized form (as defined in the psABI).
  /// Unlike parseArchString, this function will not error for unrecognized
  /// extension names or extension versions.
  ///
  /// \param Arch Normalized architecture string.
  /// \returns Parsed ISA info, or an error.
  LLVM_ABI static llvm::Expected<std::unique_ptr<RISCVISAInfo>>
  parseNormalizedArchString(StringRef Arch);

  /// Parse RISC-V ISA info from a feature vector.
  ///
  /// \param XLen Base integer register width in bits (32 or 64).
  /// \param Features Target features, each prefixed with '+' or '-'.
  /// \returns Parsed ISA info, or an error.
  LLVM_ABI static llvm::Expected<std::unique_ptr<RISCVISAInfo>>
  parseFeatures(unsigned XLen, const std::vector<std::string> &Features);

  /// Create RISC-V ISA info from an XLEN and an ordered extension map.
  ///
  /// \param XLen Base integer register width in bits (32 or 64).
  /// \param Exts Map of extension name to version.
  /// \returns Parsed ISA info, or an error.
  LLVM_ABI static llvm::Expected<std::unique_ptr<RISCVISAInfo>>
  createFromExtMap(unsigned XLen,
                   const RISCVISAUtils::OrderedExtensionMap &Exts);

  /// Convert RISC-V ISA info to a feature vector.
  ///
  /// \param AddAllExtensions If true, append disabled features for every
  ///        supported extension not present in this ISA.
  /// \param IgnoreUnknown If true, skip extensions that are not supported.
  /// \returns Feature strings prefixed with '+' or '-'.
  LLVM_ABI std::vector<std::string> toFeatures(bool AddAllExtensions = false,
                                               bool IgnoreUnknown = true) const;

  /// Return the ordered map of enabled extensions and their versions.
  ///
  /// \returns Ordered map of extension name to version.
  const RISCVISAUtils::OrderedExtensionMap &getExtensions() const {
    return Exts;
  }

  /// Return the base integer register width in bits (XLEN).
  ///
  /// \returns Base integer register width in bits.
  unsigned getXLen() const { return XLen; }
  /// Return the floating-point register width in bits (FLEN), or 0 if none.
  ///
  /// \returns Floating-point register width in bits, or 0 if none.
  unsigned getFLen() const { return FLen; }
  /// Return the minimum vector register length in bits (VLEN).
  ///
  /// \returns Minimum vector register length in bits.
  unsigned getMinVLen() const { return MinVLen; }
  /// Return the maximum vector register length in bits (VLEN).
  ///
  /// \returns Maximum vector register length in bits.
  unsigned getMaxVLen() const { return 65536; }
  /// Return the maximum EEW for integer vector loads/stores in bits.
  ///
  /// \returns Maximum EEW for integer vector loads/stores in bits.
  unsigned getMaxELen() const { return MaxELen; }
  /// Return the maximum EEW for floating-point vector loads/stores in bits.
  ///
  /// \returns Maximum EEW for floating-point vector loads/stores in bits.
  unsigned getMaxELenFp() const { return MaxELenFp; }

  /// Return true if extension \p Ext is enabled in this ISA.
  ///
  /// \param Ext Extension name, optionally with an "experimental-" prefix.
  /// \returns True if the extension is present.
  LLVM_ABI bool hasExtension(StringRef Ext) const;
  /// Return the canonical ISA string for the enabled extensions.
  ///
  /// \returns Canonical ISA string for the enabled extensions.
  LLVM_ABI std::string toString() const;
  /// Return the default ABI name implied by XLEN and enabled extensions.
  ///
  /// \returns Default ABI name for this ISA.
  LLVM_ABI StringRef computeDefaultABI() const;

  /// Return true if \p Ext is a supported RISC-V extension feature name.
  ///
  /// \param Ext Feature name, optionally with an "experimental-" prefix.
  /// \returns True if the feature names a known extension.
  LLVM_ABI static bool isSupportedExtensionFeature(StringRef Ext);
  /// Return true if \p Ext is a supported RISC-V extension name.
  ///
  /// \param Ext Extension name without version.
  /// \returns True if the extension is known.
  LLVM_ABI static bool isSupportedExtension(StringRef Ext);
  /// Return true if \p Ext names a supported extension including a version.
  ///
  /// \param Ext Extension name with an embedded version (e.g. "zba1p0").
  /// \returns True if the name and version are recognized.
  LLVM_ABI static bool isSupportedExtensionWithVersion(StringRef Ext);
  /// Return true if \p Ext is supported at the given major/minor version.
  ///
  /// \param Ext Extension name without version.
  /// \param MajorVersion Major version number.
  /// \param MinorVersion Minor version number.
  /// \returns True if that exact version is known.
  LLVM_ABI static bool isSupportedExtension(StringRef Ext,
                                            unsigned MajorVersion,
                                            unsigned MinorVersion);
  /// Return the target-feature name for extension string \p Ext.
  ///
  /// \param Ext Extension name, optionally including a version.
  /// \returns Feature name (with "experimental-" if needed), or empty if
  ///          unsupported.
  LLVM_ABI static std::string getTargetFeatureForExtension(StringRef Ext);

  /// Print all supported RISC-V -march extensions to standard output.
  ///
  /// \param DescMap Optional map from extension name to description text.
  LLVM_ABI static void printSupportedExtensions(StringMap<StringRef> &DescMap);
  /// Print the enabled RISC-V extensions for a target to standard output.
  ///
  /// \param IsRV64 True for RV64, false for RV32.
  /// \param EnabledFeatureNames Set of enabled target-feature names.
  /// \param DescMap Optional map from extension name to description text.
  LLVM_ABI static void
  printEnabledExtensions(bool IsRV64, std::set<StringRef> &EnabledFeatureNames,
                         StringMap<StringRef> &DescMap);

  /// Return the group id and bit position of __riscv_feature_bits.
  ///
  /// Returns <-1, -1> if not supported.
  ///
  /// \param Ext Extension name to look up in the feature-bits table.
  /// \returns Pair of (group id, bit position), or (-1, -1) if unsupported.
  LLVM_ABI static std::pair<int, int> getRISCVFeaturesBitsInfo(StringRef Ext);

  /// The maximum value of the group ID obtained from getRISCVFeaturesBitsInfo.
  static constexpr unsigned FeatureBitSize = 2;

private:
  RISCVISAInfo(unsigned XLen) : XLen(XLen) {}

  unsigned XLen;
  unsigned FLen = 0;
  unsigned MinVLen = 0;
  unsigned MaxELen = 0, MaxELenFp = 0;

  RISCVISAUtils::OrderedExtensionMap Exts;

  Error checkDependency();

  void updateImplication();
  void updateCombination();

  /// Update FLen, MinVLen, MaxELen, and MaxELenFp.
  void updateImpliedLengths();

  static llvm::Expected<std::unique_ptr<RISCVISAInfo>>
  postProcessAndChecking(std::unique_ptr<RISCVISAInfo> &&ISAInfo);
};

} // namespace llvm

#endif
