//===- InstrumentationMap.h - XRay Instrumentation Map ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the interface for extracting the instrumentation map from an
// XRay-instrumented binary.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_XRAY_INSTRUMENTATIONMAP_H
#define LLVM_XRAY_INSTRUMENTATIONMAP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace llvm::xray {

// Forward declare to make a friend.
class InstrumentationMap;

/// Loads the instrumentation map from |Filename|. This auto-deduces the type of
/// the instrumentation map.
/// \param Filename Path to an XRay-instrumented object file or YAML map.
/// \return The loaded instrumentation map, or an error on failure.
LLVM_ABI Expected<InstrumentationMap>
loadInstrumentationMap(StringRef Filename);

/// Represents an XRay instrumentation sled entry from an object file.
struct SledEntry {
  /// Each entry here represents the kinds of supported instrumentation map
  /// entries.
  enum class FunctionKinds {
    /// Function entry sled.
    ENTRY,
    /// Function exit sled.
    EXIT,
    /// Tail-call exit sled.
    TAIL,
    /// Function entry sled that logs arguments.
    LOG_ARGS_ENTER,
    /// Custom event sled.
    CUSTOM_EVENT
  };

  /// The address of the sled.
  uint64_t Address;

  /// The address of the function.
  uint64_t Function;

  /// The kind of sled.
  FunctionKinds Kind;

  /// Whether the sled was annotated to always be instrumented.
  bool AlwaysInstrument;

  /// Version of the sled entry encoding.
  unsigned char Version;
};

/// YAML-serializable form of an XRay instrumentation sled entry.
struct YAMLXRaySledEntry {
  /// Computed XRay function identifier for this sled.
  int32_t FuncId;
  /// Address of the sled.
  yaml::Hex64 Address;
  /// Address of the function containing the sled.
  yaml::Hex64 Function;
  /// Kind of instrumentation sled.
  SledEntry::FunctionKinds Kind;
  /// Whether the sled was annotated to always be instrumented.
  bool AlwaysInstrument;
  /// Optional human-readable name of the function.
  std::string FunctionName;
  /// Version of the sled entry encoding.
  unsigned char Version;
};

/// Maps XRay function ids to addresses from an object or YAML file.
///
/// The InstrumentationMap represents the computed function id's and indicated
/// function addresses from an object file (or a YAML file). This provides an
/// interface to just the mapping between the function id, and the function
/// address.
///
/// We also provide raw access to the actual instrumentation map entries we find
/// associated with a particular object file.
class InstrumentationMap {
public:
  /// Map from XRay function id to function address.
  using FunctionAddressMap = DenseMap<int32_t, uint64_t>;
  /// Map from function address to XRay function id.
  using FunctionAddressReverseMap = DenseMap<uint64_t, int32_t>;
  /// Container of instrumentation sled entries.
  using SledContainer = std::vector<SledEntry>;

private:
  SledContainer Sleds;
  FunctionAddressMap FunctionAddresses;
  FunctionAddressReverseMap FunctionIds;

  LLVM_ABI friend Expected<InstrumentationMap>
      loadInstrumentationMap(StringRef);

public:
  /// Provides a raw accessor to the unordered map of function addresses.
  /// \return Const reference to the map from function id to address.
  const FunctionAddressMap &getFunctionAddresses() { return FunctionAddresses; }

  /// Returns an XRay computed function id, provided a function address.
  /// \param Addr Function address to look up.
  /// \return The XRay function id, or std::nullopt if Addr is unknown.
  LLVM_ABI std::optional<int32_t> getFunctionId(uint64_t Addr) const;

  /// Returns the function address for a function id.
  /// \param FuncId XRay function id to look up.
  /// \return The function address, or std::nullopt if FuncId is unknown.
  LLVM_ABI std::optional<uint64_t> getFunctionAddr(int32_t FuncId) const;

  /// Provide read-only access to the entries of the instrumentation map.
  /// \return Const reference to the container of sled entries.
  const SledContainer &sleds() const { return Sleds; };
};

} // end namespace llvm::xray

namespace llvm {
template <>
struct yaml::ScalarEnumerationTraits<xray::SledEntry::FunctionKinds> {
  static void enumeration(IO &IO, xray::SledEntry::FunctionKinds &Kind) {
    IO.enumCase(Kind, "function-enter", xray::SledEntry::FunctionKinds::ENTRY);
    IO.enumCase(Kind, "function-exit", xray::SledEntry::FunctionKinds::EXIT);
    IO.enumCase(Kind, "tail-exit", xray::SledEntry::FunctionKinds::TAIL);
    IO.enumCase(Kind, "log-args-enter",
                xray::SledEntry::FunctionKinds::LOG_ARGS_ENTER);
    IO.enumCase(Kind, "custom-event",
                xray::SledEntry::FunctionKinds::CUSTOM_EVENT);
  }
};

template <> struct yaml::MappingTraits<xray::YAMLXRaySledEntry> {
  static void mapping(IO &IO, xray::YAMLXRaySledEntry &Entry) {
    IO.mapRequired("id", Entry.FuncId);
    IO.mapRequired("address", Entry.Address);
    IO.mapRequired("function", Entry.Function);
    IO.mapRequired("kind", Entry.Kind);
    IO.mapRequired("always-instrument", Entry.AlwaysInstrument);
    IO.mapOptional("function-name", Entry.FunctionName);
    IO.mapOptional("version", Entry.Version, 0);
  }

  static constexpr bool flow = true;
};

/// Sequences of YAML XRay sled entries use block formatting.
template <> struct yaml::SequenceElementTraits<xray::YAMLXRaySledEntry> {
  /// Emit sequences of YAML XRay sled entries in block style.
  static const bool flow = false;
};
} // namespace llvm

#endif // LLVM_XRAY_INSTRUMENTATIONMAP_H
