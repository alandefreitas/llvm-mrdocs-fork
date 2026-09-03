//===- YAMLXRayRecord.h - XRay Record YAML Support Definitions ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Types and traits specialisations for YAML I/O of XRay log entries.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_YAMLXRAYRECORD_H
#define LLVM_XRAY_YAMLXRAYRECORD_H

#include "llvm/Support/YAMLTraits.h"
#include "llvm/XRay/XRayRecord.h"

namespace llvm::xray {

/// YAML-serializable form of an XRay file header.
struct YAMLXRayFileHeader {
  /// Version of the XRay implementation that produced this file.
  uint16_t Version;

  /// A numeric identifier for the type of file this is.
  uint16_t Type;

  /// Whether the CPU that produced the timestamp counters (TSC) move at a
  /// constant rate.
  bool ConstantTSC;

  /// Whether the CPU that produced the timestamp counters (TSC) do not stop.
  bool NonstopTSC;

  /// The number of cycles per second for the CPU that produced the timestamp
  /// counter (TSC) values.
  uint64_t CycleFrequency;
};

/// YAML-serializable form of a denormalized XRay trace record.
struct YAMLXRayRecord {
  /// Record type subtype used with \c Type; always 0 for function and custom
  /// events, and the typed-event kind for typed events.
  uint16_t RecordType;

  /// The CPU where the thread is running.
  uint16_t CPU;

  /// Identifies the type of record.
  RecordTypes Type;

  /// The function ID for the record, if this is a function call record.
  int32_t FuncId;

  /// Optional human-readable name associated with \c FuncId.
  std::string Function;

  /// Full 8-byte timestamp counter value when the log record was taken.
  uint64_t TSC;

  /// The thread ID for the currently running thread.
  uint32_t TId;

  /// The process ID for the currently running process.
  uint32_t PId;

  /// The function call arguments.
  std::vector<uint64_t> CallArgs;

  /// For custom and typed events, the raw data from the trace.
  std::string Data;
};

/// YAML-serializable representation of a complete XRay trace.
struct YAMLXRayTrace {
  /// File header describing how to interpret the trace records.
  YAMLXRayFileHeader Header;

  /// Ordered list of denormalized XRay records in the trace.
  std::vector<YAMLXRayRecord> Records;
};

} // namespace llvm::xray

namespace llvm {
// YAML Traits
// -----------

/// YAMLIO scalar enumeration traits for \c xray::RecordTypes.
template <> struct yaml::ScalarEnumerationTraits<xray::RecordTypes> {
  /// Map XRay record type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Type Record type being mapped.
  static void enumeration(IO &IO, xray::RecordTypes &Type) {
    IO.enumCase(Type, "function-enter", xray::RecordTypes::ENTER);
    IO.enumCase(Type, "function-exit", xray::RecordTypes::EXIT);
    IO.enumCase(Type, "function-tail-exit", xray::RecordTypes::TAIL_EXIT);
    IO.enumCase(Type, "function-enter-arg", xray::RecordTypes::ENTER_ARG);
    IO.enumCase(Type, "custom-event", xray::RecordTypes::CUSTOM_EVENT);
    IO.enumCase(Type, "typed-event", xray::RecordTypes::TYPED_EVENT);
  }
};

/// YAMLIO mapping traits for \c xray::YAMLXRayFileHeader.
template <> struct yaml::MappingTraits<xray::YAMLXRayFileHeader> {
  /// Map an XRay file header to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Header File header being mapped.
  static void mapping(IO &IO, xray::YAMLXRayFileHeader &Header) {
    IO.mapRequired("version", Header.Version);
    IO.mapRequired("type", Header.Type);
    IO.mapRequired("constant-tsc", Header.ConstantTSC);
    IO.mapRequired("nonstop-tsc", Header.NonstopTSC);
    IO.mapRequired("cycle-frequency", Header.CycleFrequency);
  }
};

/// YAMLIO mapping traits for \c xray::YAMLXRayRecord.
template <> struct yaml::MappingTraits<xray::YAMLXRayRecord> {
  /// Map a denormalized XRay record to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Record Trace record being mapped.
  static void mapping(IO &IO, xray::YAMLXRayRecord &Record) {
    IO.mapRequired("type", Record.RecordType);
    IO.mapOptional("func-id", Record.FuncId);
    IO.mapOptional("function", Record.Function);
    IO.mapOptional("args", Record.CallArgs);
    IO.mapRequired("cpu", Record.CPU);
    IO.mapOptional("thread", Record.TId, 0U);
    IO.mapOptional("process", Record.PId, 0U);
    IO.mapRequired("kind", Record.Type);
    IO.mapRequired("tsc", Record.TSC);
    IO.mapOptional("data", Record.Data);
  }

  /// Emit YAMLXRayRecord mappings in flow style.
  static constexpr bool flow = true;
};

/// YAMLIO mapping traits for \c xray::YAMLXRayTrace.
template <> struct yaml::MappingTraits<llvm::xray::YAMLXRayTrace> {
  /// Map a complete XRay trace (header and records) to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Trace Trace being mapped.
  static void mapping(IO &IO, xray::YAMLXRayTrace &Trace) {
    // A trace file contains two parts, the header and the list of all the
    // trace records.
    IO.mapRequired("header", Trace.Header);
    IO.mapRequired("records", Trace.Records);
  }
};

namespace yaml {

/// Sequences of YAML XRay records use block formatting.
template <> struct SequenceElementTraits<xray::YAMLXRayRecord> {
  /// Emit sequences of YAML XRay records in block style.
  static const bool flow = false;
};

} // namespace yaml
} // namespace llvm

#endif // LLVM_XRAY_YAMLXRAYRECORD_H
