//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides the basic framework for Telemetry.
/// Refer to its documentation at llvm/docs/Telemetry.rst for more details.
//===---------------------------------------------------------------------===//

#ifndef LLVM_TELEMETRY_TELEMETRY_H
#define LLVM_TELEMETRY_TELEMETRY_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
/// Framework for collecting and transmitting LLVM tool telemetry.
namespace telemetry {

/// Abstract serializer for writing keyed telemetry fields.
class Serializer {
public:
  /// Destroy the serializer.
  virtual ~Serializer() = default;

  /// Initialize the serializer before writing any fields.
  ///
  /// @return Success, or an error if initialization failed.
  virtual Error init() = 0;
  /// Write a boolean value under \p KeyName.
  /// @param KeyName Key under which to store the value.
  /// @param Value Boolean value to write.
  virtual void write(StringRef KeyName, bool Value) = 0;
  /// Write a string value under \p KeyName.
  /// @param KeyName Key under which to store the value.
  /// @param Value String value to write.
  virtual void write(StringRef KeyName, StringRef Value) = 0;
  /// Write a signed int value under \p KeyName.
  /// @param KeyName Key under which to store the value.
  /// @param Value Integer value to write.
  virtual void write(StringRef KeyName, int Value) = 0;
  /// Write a signed long value under \p KeyName.
  /// @param KeyName Key under which to store the value.
  /// @param Value Integer value to write.
  virtual void write(StringRef KeyName, long Value) = 0;
  /// Write a signed long long value under \p KeyName.
  /// @param KeyName Key under which to store the value.
  /// @param Value Integer value to write.
  virtual void write(StringRef KeyName, long long Value) = 0;
  /// Write an unsigned int value under \p KeyName.
  /// @param KeyName Key under which to store the value.
  /// @param Value Integer value to write.
  virtual void write(StringRef KeyName, unsigned int Value) = 0;
  /// Write an unsigned long value under \p KeyName.
  /// @param KeyName Key under which to store the value.
  /// @param Value Integer value to write.
  virtual void write(StringRef KeyName, unsigned long Value) = 0;
  /// Write an unsigned long long value under \p KeyName.
  /// @param KeyName Key under which to store the value.
  /// @param Value Integer value to write.
  virtual void write(StringRef KeyName, unsigned long long Value) = 0;
  /// Begin a nested object under \p KeyName.
  /// @param KeyName Key under which to open the nested object.
  virtual void beginObject(StringRef KeyName) = 0;
  /// End the current nested object.
  virtual void endObject() = 0;
  /// Finalize the serializer after all fields have been written.
  ///
  /// @return Success, or an error if finalization failed.
  virtual Error finalize() = 0;

  /// Write a string-keyed map as a nested object under \p KeyName.
  ///
  /// Keys must be convertible to \c StringRef. Each entry is written with the
  /// corresponding \c write overload for its mapped type.
  /// @param KeyName Key under which to store the map as an object.
  /// @param Map Map whose entries are serialized as object fields.
  template <typename T, typename = typename T::mapped_type>
  void write(StringRef KeyName, const T &Map) {
    static_assert(std::is_convertible_v<typename T::key_type, StringRef>,
                  "KeyType must be convertible to string");
    beginObject(KeyName);
    for (const auto &KeyVal : Map)
      write(KeyVal.first, KeyVal.second);
    endObject();
  }
};

/// Configuration for the Manager class.
///
/// This stores configurations from both users and vendors and is passed
/// to the Manager upon construction. (Any changes to the config after
/// the Manager's construction will not have any effect on it).
///
/// This struct can be extended as needed to add additional configuration
/// points specific to a vendor's implementation.
struct Config {
  /// Whether telemetry support was compiled into this build.
  static constexpr bool BuildTimeEnableTelemetry = LLVM_ENABLE_TELEMETRY;

  /// Whether telemetry is enabled at runtime for this configuration.
  const bool EnableTelemetry;

  /// Construct a config using the build-time telemetry enablement flag.
  explicit Config() : EnableTelemetry(BuildTimeEnableTelemetry) {}

  /// Destroy the configuration.
  virtual ~Config() = default;

  /// Construct a config enabled only if both runtime and build-time flags allow it.
  ///
  /// Telemetry can only be enabled if both the runtime and buildtime flag
  /// are set.
  /// @param E Runtime request to enable telemetry; AND-ed with the build-time flag.
  explicit Config(bool E) : EnableTelemetry(E && BuildTimeEnableTelemetry) {}

  /// Create a session identifier for a new telemetry session, if any.
  ///
  /// @return A session id string, or \c std::nullopt if none is provided.
  virtual std::optional<std::string> makeSessionId() { return std::nullopt; }
};

/// For isa, dyn_cast, etc operations on TelemetryInfo.
typedef unsigned KindType;
/// Kind tags used by TelemetryInfo for isa and dyn_cast.
///
/// This struct is used by TelemetryInfo to support isa<>, dyn_cast<>
/// operations.
/// It is defined as a struct (rather than an enum) because it is
/// expected to be extended by subclasses which may have
/// additional TelemetryInfo types defined to describe different events.
struct EntryKind {
  /// Kind value for the base TelemetryInfo type.
  static const KindType Base = 0;
};

/// TelemetryInfo is the data courier, used to move instrumented data
/// from the tool being monitored to the Telemetry framework.
///
/// This base class contains only the basic set of telemetry data.
/// Downstream implementations can define more subclasses with
/// additional fields to describe different events and concepts.
///
/// For example, The LLDB debugger can define a DebugCommandInfo subclass
/// which has additional fields about the debug-command being instrumented,
/// such as `CommandArguments` or `CommandName`.
struct LLVM_ABI TelemetryInfo {
  /// Unique id for a tool session from start until exit.
  ///
  /// Conventionally this corresponds to a tool's session. A tool could have
  /// multiple sessions running at once, in which case there shall be multiple
  /// sets of TelemetryInfo with multiple unique IDs.
  ///
  /// Different usages can assign different types of IDs to this field.
  std::string SessionId;

  /// Construct an empty TelemetryInfo with default field values.
  TelemetryInfo() = default;
  /// Destroy the TelemetryInfo.
  virtual ~TelemetryInfo() = default;

  /// Serialize this entry's fields into \p serializer.
  /// @param serializer Destination serializer that receives the fields.
  virtual void serialize(Serializer &serializer) const;

  /// Return the dynamic kind tag for isa and dyn_cast.
  /// @return Dynamic kind tag used for isa and dyn_cast.
  virtual KindType getKind() const { return EntryKind::Base; }
  /// Return true if \p T has the base TelemetryInfo kind.
  /// @param T Entry to test; must not be null.
  /// @return True if \p T has the base TelemetryInfo kind.
  static bool classof(const TelemetryInfo *T) {
    return T->getKind() == EntryKind::Base;
  }
};

/// This class presents a data sink to which the Telemetry framework
/// sends data.
///
/// Its implementation is transparent to the framework.
/// It is up to the vendor to decide which pieces of data to forward
/// and where to forward them.
class Destination {
public:
  /// Destroy the destination.
  virtual ~Destination() = default;
  /// Receive a telemetry entry for forwarding or storage.
  /// @param Entry Telemetry entry to receive; must not be null.
  /// @return Success, or an error if the entry could not be received.
  virtual Error receiveEntry(const TelemetryInfo *Entry) = 0;
  /// Return a stable name identifying this destination.
  /// @return Stable name identifying this destination.
  virtual StringLiteral name() const = 0;
};

/// Main interaction point between an LLVM tool and the telemetry framework.
///
/// It is responsible for collecting telemetry data from the tool being
/// monitored and transmitting the data elsewhere.
class LLVM_ABI Manager {
public:
  /// Construct an empty Manager with no destinations.
  Manager() = default;
  /// Destroy the Manager.
  virtual ~Manager() = default;

  /// Copy construction is disabled; Managers are non-copyable.
  /// @param Other Unused; copy construction is not supported.
  Manager(Manager const &Other) = delete;
  /// Copy assignment is disabled; Managers are non-copyable.
  /// @param Other Unused; copy assignment is not supported.
  Manager &operator=(Manager const &Other) = delete;

  /// Dispatch telemetry data to the registered Destination(s).
  ///
  /// The argument is non-const because the Manager may add or remove
  /// data from the entry.
  /// @param Entry Telemetry entry to dispatch; may be modified by the Manager.
  /// @return Success, or an error if dispatch failed.
  virtual Error dispatch(TelemetryInfo *Entry);

  /// Register a Destination that will receive dispatched entries.
  /// @param Destination Destination to take ownership of and register.
  void addDestination(std::unique_ptr<Destination> Destination);

protected:
  /// Optional callback for subclasses before dispatching to Destinations.
  /// @param Entry Telemetry entry about to be dispatched; may be modified.
  /// @return Success, or an error that aborts dispatch.
  virtual Error preDispatch(TelemetryInfo *Entry);

private:
  std::vector<std::unique_ptr<Destination>> Destinations;
};

} // namespace telemetry
} // namespace llvm

#endif // LLVM_TELEMETRY_TELEMETRY_H
