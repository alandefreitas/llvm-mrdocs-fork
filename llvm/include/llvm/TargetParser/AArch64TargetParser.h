//===-- AArch64TargetParser - Parser for AArch64 features -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise AArch64 hardware features
// such as FPU/CPU/ARCH and extension names.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_AARCH64TARGETPARSER_H
#define LLVM_TARGETPARSER_AARCH64TARGETPARSER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Bitset.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringTable.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <set>
#include <vector>

namespace llvm {

class Triple;

/// AArch64 CPU, architecture, and extension name parsing helpers.
namespace AArch64 {

struct ArchInfo;
struct CpuInfo;

#include "llvm/TargetParser/AArch64CPUFeatures.inc"
#include "llvm/TargetParser/AArch64FeatPriorities.inc"

static_assert(FEAT_MAX < 62,
              "Number of features in CPUFeatures are limited to 62 entries");

static_assert(PRIOR_MAX < 120, "FeatPriorities is limited to 120 entries");

// Emit the StringTable StrTab to which all offsets refer.
#define EMIT_STRTAB
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

// Each ArchExtKind correponds directly to a possible -target-feature.
#define EMIT_ARCHEXTKIND_ENUM
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

/// Bitset of enabled or touched AArch64 architecture extensions.
using ExtensionBitset = Bitset<AEK_NUM_EXTENSIONS>;

/// Description of an architecture extension selectable via -march.
///
/// Typically these correspond to Arm Architecture extensions, unlike
/// SubtargetFeature which may represent either an actual extension or some
/// internal LLVM property.
struct ExtensionInfo {
  /// Human-readable name used in -march, -cpu, and target function attributes.
  StringTable::Offset UserVisibleName;
  /// Alternate spelling for this extension, if one exists.
  StringTable::Offset Alias;
  /// Bitfield enumerator for this extension.
  ArchExtKind ID;
  /// Architecture feature name, e.g. FEAT_AdvSIMD.
  StringTable::Offset ArchFeatureName;
  /// Textual description of the extension.
  StringTable::Offset Description;
  /// -target-feature/-mattr enable string, e.g. "+spe".
  StringTable::Offset PosTargetFeature;
  /// -target-feature/-mattr disable string, e.g. "-spe".
  StringTable::Offset NegTargetFeature;
};

#define EMIT_EXTENSIONS
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

/// Function multi-versioning feature metadata for one FMV spelling.
struct FMVInfo {
  /// Spelling used in target_version / target_clones attributes.
  StringRef Name;
  /// Index of the bit in the FMV feature bitset, if any.
  std::optional<CPUFeatures> FeatureBit;
  /// Index of the bit in the FMV priority bitset.
  FeatPriorities PriorityBit;
  /// Architecture extension enabled by this FMV feature, if any.
  std::optional<ArchExtKind> ID;
  /// Construct FMV metadata for \p Name with the given bit indices and extension.
  /// @param Name target_version / target_clones spelling.
  /// @param FeatureBit Optional index into the FMV feature bitset.
  /// @param PriorityBit Index into the FMV priority bitset.
  /// @param ID Optional architecture extension to enable.
  FMVInfo(StringRef Name, std::optional<CPUFeatures> FeatureBit,
          FeatPriorities PriorityBit, std::optional<ArchExtKind> ID)
      : Name(Name), FeatureBit(FeatureBit), PriorityBit(PriorityBit), ID(ID) {};
};

/// Return the table of known function multi-versioning (FMV) features.
/// @return Constant reference to the FMV feature table.
LLVM_ABI const std::vector<FMVInfo> &getFMVInfo();

/// Dependency between two architecture extensions.
///
/// Later is the feature which was added to the architecture after Earlier, and
/// expands the functionality provided by it. If Later is enabled, then Earlier
/// will also be enabled. If Earlier is disabled, then Later will also be
/// disabled.
struct ExtensionDependency {
  /// Earlier extension implied when the later one is enabled.
  ArchExtKind Earlier;
  /// Later extension that depends on the earlier one.
  ArchExtKind Later;
};

#define EMIT_EXTENSION_DEPENDENCIES
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

/// AArch64 architecture profile kind.
enum ArchProfile {
  AProfile = 'A',        ///< Application profile.
  RProfile = 'R',        ///< Real-time profile.
  InvalidProfile = '?'   ///< Unknown or invalid profile.
};

/// Information about a specific architecture version, e.g. V8.1-A.
struct ArchInfo {
  /// Architecture version, major + minor.
  VersionTuple Version;
  /// Architecture profile (A, R, or invalid).
  ArchProfile Profile;
  /// Name as supplied to -march, e.g. "armv8.1-a".
  StringTable::Offset Name;
  /// Name as supplied to -target-feature, e.g. "+v8a".
  StringTable::Offset ArchFeature;
  /// Bitfield of default extensions for this architecture.
  AArch64::ExtensionBitset DefaultExts;

  /// Return true if this architecture has the same name as \p Other.
  /// @param Other Architecture to compare against.
  /// @return True when the names are equal.
  bool operator==(const ArchInfo &Other) const {
    return this->Name == Other.Name;
  }
  /// Return true if this architecture has a different name from \p Other.
  /// @param Other Architecture to compare against.
  /// @return True when the names differ.
  bool operator!=(const ArchInfo &Other) const {
    return this->Name != Other.Name;
  }

  /// Return true if this architecture implies the features of \p Other.
  ///
  /// Defines the following partial order, indicating when an architecture is
  /// a superset of another:
  ///
  /// v9.7a > v9.6a > v9.5a > v9.4a > v9.3a > v9.2a > v9.1a > v9a;
  ///                           v       v       v       v       v
  ///                         v8.9a > v8.8a > v8.7a > v8.6a > v8.5a > ... > v8a;
  ///
  /// v8r has no relation to anything. This is used to determine which
  /// features to enable for a given architecture. See
  /// AArch64TargetInfo::setFeatureEnabled.
  /// @param Other Architecture that may be implied by this one.
  /// @return True when this architecture implies \p Other.
  bool implies(const ArchInfo &Other) const {
    if (this->Profile != Other.Profile)
      return false; // ARMV8R
    if (this->Version.getMajor() == Other.Version.getMajor()) {
      return this->Version > Other.Version;
    }
    if (this->Version.getMajor() == 9 && Other.Version.getMajor() == 8) {
      assert(this->Version.getMinor() && Other.Version.getMinor() &&
             "AArch64::ArchInfo should have a minor version.");
      return this->Version.getMinor().value_or(0) + 5 >=
             Other.Version.getMinor().value_or(0);
    }
    return false;
  }

  /// Return true if this architecture is a superset of \p Other (or equal).
  /// @param Other Architecture to test against.
  /// @return True when this equals or implies \p Other.
  bool is_superset(const ArchInfo &Other) const {
    return (*this == Other) || implies(Other);
  }

  /// Return the architecture feature name without the leading "+".
  /// @return Architecture feature name without a leading "+".
  StringRef getSubArch() const { return StrTab[ArchFeature].substr(1); }

  /// Search for ArchInfo by SubArch name.
  /// @param SubArch Sub-architecture name without a leading "+".
  /// @return Matching ArchInfo, or std::nullopt if not found.
  LLVM_ABI static std::optional<ArchInfo> findBySubArch(StringRef SubArch);
};

#define EMIT_ARCHITECTURES
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

/// Details of a specific CPU known to the AArch64 target parser.
struct CpuInfo {
  /// CPU name as written for -mcpu.
  StringTable::Offset Name;
  /// Index of this CPU's default architecture in the architecture table.
  unsigned ArchIdx;
  /// Default extensions enabled for this CPU.
  AArch64::ExtensionBitset DefaultExtensions;
};

#define EMIT_CPU_INFO
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

/// Mutable set of enabled AArch64 architecture extensions and base arch.
struct ExtensionSet {
  /// Set of extensions which are currently enabled.
  ExtensionBitset Enabled;
  /// Extensions enabled or disabled at any point (avoids cluttering -cc1).
  ExtensionBitset Touched;
  /// Base architecture version used when resolving version-dependent deps.
  const ArchInfo *BaseArch;

  /// Construct an empty extension set with no base architecture.
  ExtensionSet() : Enabled(), Touched(), BaseArch(nullptr) {}

  /// Enable \p E and any extensions it depends on.
  ///
  /// Does not change the base architecture, or follow dependencies between
  /// features which are only related by required architecture versions.
  /// @param E Architecture extension to enable.
  LLVM_ABI void enable(ArchExtKind E);

  /// Disable \p E and any extensions which depend on it.
  ///
  /// Does not change the base architecture, or follow dependencies between
  /// features which are only related by required architecture versions.
  /// @param E Architecture extension to disable.
  LLVM_ABI void disable(ArchExtKind E);

  /// Add default extensions for \p CPU and record its base architecture.
  /// @param CPU CPU whose defaults should be applied.
  LLVM_ABI void addCPUDefaults(const CpuInfo &CPU);

  /// Add default extensions for \p Arch and record it as the base architecture.
  /// @param Arch Architecture whose defaults should be applied.
  LLVM_ABI void addArchDefaults(const ArchInfo &Arch);

  /// Enable or disable an extension from a modifier string.
  ///
  /// The string must be of the form "<name>" to enable a feature or "no<name>"
  /// to disable it. This will also enable or disable any features as required
  /// by the dependencies between them.
  /// @param Modifier Extension name, optionally prefixed with "no".
  /// @param AllowNoDashForm Also accept "no-<name>" as a disable form.
  /// @return True if the modifier was recognized and applied.
  LLVM_ABI bool parseModifier(StringRef Modifier,
                              const bool AllowNoDashForm = false);

  /// Rebuild this set from parsed feature strings without expanding deps.
  ///
  /// Constructs a new ExtensionSet by toggling the corresponding bits for every
  /// feature in the \p Features list without expanding their dependencies. Used
  /// for reconstructing an ExtensionSet from the output of toLLVMFeatures().
  /// Features that are not recognized are pushed back to \p NonExtensions.
  /// @param Features Feature strings previously emitted for this set.
  /// @param NonExtensions Receives unrecognized entries from \p Features.
  LLVM_ABI void
  reconstructFromParsedFeatures(const std::vector<std::string> &Features,
                                std::vector<std::string> &NonExtensions);

  /// Append LLVM -target-feature strings for enabled and disabled extensions.
  /// @param Features Output vector that receives feature strings.
  template <typename T> void toLLVMFeatureList(std::vector<T> &Features) const {
    if (BaseArch && !StrTab[BaseArch->ArchFeature].empty())
      Features.emplace_back(T(StrTab[BaseArch->ArchFeature]));

    for (const auto &E : Extensions) {
      if (!Touched.test(E.ID))
        continue;
      if (Enabled.test(E.ID))
        Features.emplace_back(T(StrTab[E.PosTargetFeature]));
      else
        Features.emplace_back(T(StrTab[E.NegTargetFeature]));
    }
  }

  /// Print the enabled extensions for debugging.
  LLVM_ABI void dump() const;
};

/// Alternate name mapping for a CPU or similar identifier.
struct Alias {
  /// Alternate spelling that should resolve to \c Name.
  StringTable::Offset AltName;
  /// Canonical name that \c AltName aliases.
  StringTable::Offset Name;
};

#define EMIT_CPU_ALIAS
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

/// Return the ExtensionInfo for architecture extension \p ExtID.
/// @param ExtID Architecture extension enumerator.
/// @return Reference to the matching ExtensionInfo entry.
LLVM_ABI const ExtensionInfo &getExtensionByID(ArchExtKind(ExtID));

/// Convert enabled extensions into LLVM -target-feature name strings.
/// @param Extensions Bitset of extensions to convert.
/// @param Features Receives the corresponding feature name strings.
/// @return True on success.
LLVM_ABI bool getExtensionFeatures(const AArch64::ExtensionBitset &Extensions,
                                   std::vector<StringRef> &Features);

/// Return the -target-feature enable/disable string for \p ArchExt.
/// @param ArchExt Architecture extension name, optionally with a "no" prefix.
/// @return Matching feature string, or empty if unrecognized.
LLVM_ABI StringRef getArchExtFeature(StringRef ArchExt);
/// Resolve a CPU alias to its canonical CPU name.
/// @param CPU CPU name or alias.
/// @return Canonical CPU name, or \p CPU if it is not an alias.
LLVM_ABI StringRef resolveCPUAlias(StringRef CPU);

/// Return the default architecture for CPU name \p CPU.
/// @param CPU CPU name (aliases are resolved first).
/// @return Pointer to the matching ArchInfo, or nullptr if unknown.
LLVM_ABI const ArchInfo *getArchForCpu(StringRef CPU);

/// Parse an architecture name as accepted by -march.
/// @param Arch Architecture name string.
/// @return Pointer to the matching ArchInfo, or nullptr if unrecognized.
LLVM_ABI const ArchInfo *parseArch(StringRef Arch);

/// Return the extension which has the given -target-feature name.
/// @param TargetFeature Feature string such as "+spe" or "-spe".
/// @return Matching ExtensionInfo, or std::nullopt if unrecognized.
LLVM_ABI std::optional<ExtensionInfo>
targetFeatureToExtension(StringRef TargetFeature);

/// Parse a name as defined by the Extension class in tablegen.
/// @param Extension Extension name as used in -march modifiers.
/// @return Matching ExtensionInfo, or std::nullopt if unrecognized.
LLVM_ABI std::optional<ExtensionInfo> parseArchExtension(StringRef Extension);

/// Parse a name as defined by the FMVInfo class in tablegen.
/// @param Extension FMV feature spelling.
/// @return Matching FMVInfo, or std::nullopt if unrecognized.
LLVM_ABI std::optional<FMVInfo> parseFMVExtension(StringRef Extension);

/// Given the name of a CPU or alias, return the corresponding CpuInfo.
/// @param Name CPU name or alias.
/// @return Matching CpuInfo, or std::nullopt if unrecognized.
LLVM_ABI std::optional<CpuInfo> parseCpu(StringRef Name);
/// Append all valid CPU and architecture names to \p Values.
/// @param Values Output list populated with recognized names.
LLVM_ABI void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values);

/// Return true if register X18 is reserved by default for triple \p TT.
/// @param TT Target triple whose OS/environment defaults are consulted.
/// @return True when X18 is reserved by default.
LLVM_ABI bool isX18ReservedByDefault(const Triple &TT);

/// Compute the FMV priority bitmask for the given feature names.
///
/// For a given set of feature names, which can be either target-features, or
/// fmv-features metadata, expand their dependencies and then return a bitmask
/// corresponding to the entries of AArch64::FeatPriorities.
/// @param Features Feature or FMV feature names.
/// @return Priority bitmask over AArch64::FeatPriorities.
LLVM_ABI APInt getFMVPriority(ArrayRef<StringRef> Features);

/// Compute the CPU-supports bitmask for the given FMV feature names.
///
/// For a given set of FMV feature names, expand their dependencies and then
/// return a bitmask corresponding to the entries of AArch64::CPUFeatures.
/// The values in CPUFeatures are not bitmasks themselves, they are sequential
/// (0, 1, 2, 3, ...). The resulting bitmask is used at runtime to test whether
/// a certain FMV feature is available on the host.
/// @param Features FMV feature names.
/// @return Bitmask over AArch64::CPUFeatures indices.
LLVM_ABI APInt getCpuSupportsMask(ArrayRef<StringRef> Features);

/// Print the list of supported AArch64 architecture extensions.
LLVM_ABI void PrintSupportedExtensions();

/// Print the subset of extensions currently enabled by \p EnabledFeatureNames.
/// @param EnabledFeatureNames Feature names that are currently enabled.
LLVM_ABI void
printEnabledExtensions(const std::set<StringRef> &EnabledFeatureNames);

} // namespace AArch64
} // namespace llvm

#endif
