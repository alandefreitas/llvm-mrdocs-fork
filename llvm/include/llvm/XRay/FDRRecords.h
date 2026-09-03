//===- FDRRecords.h - XRay Flight Data Recorder Mode Records --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Define types and operations on these types that represent the different kinds
// of records we encounter in XRay flight data recorder mode traces.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_FDRRECORDS_H
#define LLVM_XRAY_FDRRECORDS_H

#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <string>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/XRay/XRayRecord.h"

namespace llvm::xray {

class RecordVisitor;
class RecordInitializer;

/// Base class for a single record in an XRay FDR-mode trace.
class Record {
public:
  /// Discriminator for concrete FDR record types in the class hierarchy.
  enum class RecordKind {
    /// Base metadata record kind.
    RK_Metadata,
    /// Buffer-extents metadata record.
    RK_Metadata_BufferExtents,
    /// Wall-clock time metadata record.
    RK_Metadata_WallClockTime,
    /// New CPU ID metadata record.
    RK_Metadata_NewCPUId,
    /// TSC wrap metadata record.
    RK_Metadata_TSCWrap,
    /// Custom event metadata record (pre-v5).
    RK_Metadata_CustomEvent,
    /// Custom event metadata record (v5+).
    RK_Metadata_CustomEventV5,
    /// Call-argument metadata record.
    RK_Metadata_CallArg,
    /// Process ID entry metadata record.
    RK_Metadata_PIDEntry,
    /// New buffer metadata record.
    RK_Metadata_NewBuffer,
    /// End-of-buffer metadata record.
    RK_Metadata_EndOfBuffer,
    /// Typed event metadata record.
    RK_Metadata_TypedEvent,
    /// Sentinel one past the last metadata kind.
    RK_Metadata_LastMetadata,
    /// Function enter/exit record.
    RK_Function,
  };

  /// Return a human-readable name for record kind \p K.
  /// \param K Record kind to convert.
  /// \return Human-readable name for \p K.
  LLVM_ABI static StringRef kindToString(RecordKind K);

private:
  const RecordKind T;

public:
  /// Copy construction is deleted; records are not copyable.
  /// \param Other Record that would be copied.
  Record(const Record &Other) = delete;
  /// Move construction is deleted; records are not movable.
  /// \param Other Record that would be moved.
  Record(Record &&Other) = delete;
  /// Copy assignment is deleted; records are not copyable.
  /// \param Other Record that would be copied.
  Record &operator=(const Record &Other) = delete;
  /// Move assignment is deleted; records are not movable.
  /// \param Other Record that would be moved.
  Record &operator=(Record &&Other) = delete;
  /// Construct a record with kind discriminator \p T.
  /// \param T Record kind for this instance.
  explicit Record(RecordKind T) : T(T) {}

  /// Return the kind discriminator for this record.
  /// \return The kind discriminator for this record.
  RecordKind getRecordType() const { return T; }

  /// Apply visitor \p V to this record, dispatching on the dynamic type.
  ///
  /// Each Record should be able to apply an abstract visitor, and choose the
  /// appropriate function in the visitor to invoke, given its own type.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  virtual Error apply(RecordVisitor &V) = 0;

  /// Destroy the record.
  virtual ~Record() = default;
};

/// Base class for FDR metadata records that are not function enter/exit events.
class MetadataRecord : public Record {
public:
  /// Kind of metadata payload carried by a MetadataRecord subclass.
  enum class MetadataType : unsigned {
    /// Unknown or unrecognized metadata type.
    Unknown,
    /// Buffer size extents for the following records.
    BufferExtents,
    /// Absolute wall-clock timestamp.
    WallClockTime,
    /// Switch to a new CPU identifier.
    NewCPUId,
    /// Absolute TSC base after a wrap.
    TSCWrap,
    /// User-defined custom event payload.
    CustomEvent,
    /// Argument value for a preceding function enter-with-args.
    CallArg,
    /// Process identifier for the following buffer.
    PIDEntry,
    /// Start of a new per-thread buffer.
    NewBuffer,
    /// End of the current buffer.
    EndOfBuffer,
    /// Typed user event with an event-type tag.
    TypedEvent,
  };

protected:
  /// Fixed size in bytes of a metadata record body after the type header.
  static constexpr int kMetadataBodySize = 15;
  friend class RecordInitializer;

private:
  const MetadataType MT;

public:
  /// Construct a metadata record with kind \p T and metadata type \p M.
  /// \param T Record kind discriminator.
  /// \param M Metadata subtype for this record.
  explicit MetadataRecord(RecordKind T, MetadataType M) : Record(T), MT(M) {}

  /// Return true if \p R is a metadata record.
  /// \param R Record to test.
  /// \return True if \p R is a metadata record.
  static bool classof(const Record *R) {
    return R->getRecordType() >= RecordKind::RK_Metadata &&
           R->getRecordType() <= RecordKind::RK_Metadata_LastMetadata;
  }

  /// Return the metadata subtype for this record.
  /// \return The metadata subtype for this record.
  MetadataType metadataType() const { return MT; }

  /// Destroy the metadata record.
  ~MetadataRecord() override = default;
};

// What follows are specific Metadata record types which encapsulate the
// information associated with specific metadata record types in an FDR mode
// log.

/// Metadata record giving the size in bytes of a following FDR buffer.
class LLVM_ABI BufferExtents : public MetadataRecord {
  uint64_t Size = 0;
  friend class RecordInitializer;

public:
  /// Construct an empty buffer-extents record with size zero.
  BufferExtents()
      : MetadataRecord(RecordKind::RK_Metadata_BufferExtents,
                       MetadataType::BufferExtents) {}

  /// Construct a buffer-extents record for a buffer of size \p S.
  /// \param S Size of the buffer in bytes.
  explicit BufferExtents(uint64_t S)
      : MetadataRecord(RecordKind::RK_Metadata_BufferExtents,
                       MetadataType::BufferExtents),
        Size(S) {}

  /// Return the size of the buffer in bytes.
  /// \return The size of the buffer in bytes.
  uint64_t size() const { return Size; }

  /// Apply visitor \p V to this buffer-extents record.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  Error apply(RecordVisitor &V) override;

  /// Return true if \p R is a BufferExtents record.
  /// \param R Record to test.
  /// \return True if \p R is a BufferExtents record.
  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_BufferExtents;
  }
};

/// Metadata record holding an absolute wall-clock timestamp.
class LLVM_ABI WallclockRecord : public MetadataRecord {
  uint64_t Seconds = 0;
  uint32_t Nanos = 0;
  friend class RecordInitializer;

public:
  /// Construct an empty wall-clock record with a zero timestamp.
  WallclockRecord()
      : MetadataRecord(RecordKind::RK_Metadata_WallClockTime,
                       MetadataType::WallClockTime) {}

  /// Construct a wall-clock record for \p S seconds and \p N nanoseconds.
  /// \param S Seconds since the epoch.
  /// \param N Additional nanoseconds.
  explicit WallclockRecord(uint64_t S, uint32_t N)
      : MetadataRecord(RecordKind::RK_Metadata_WallClockTime,
                       MetadataType::WallClockTime),
        Seconds(S), Nanos(N) {}

  /// Return the seconds component of the wall-clock time.
  /// \return The seconds component of the wall-clock time.
  uint64_t seconds() const { return Seconds; }
  /// Return the nanoseconds component of the wall-clock time.
  /// \return The nanoseconds component of the wall-clock time.
  uint32_t nanos() const { return Nanos; }

  /// Apply visitor \p V to this wall-clock record.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  Error apply(RecordVisitor &V) override;

  /// Return true if \p R is a WallclockRecord.
  /// \param R Record to test.
  /// \return True if \p R is a WallclockRecord.
  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_WallClockTime;
  }
};

/// Metadata record marking a switch to a new CPU and its absolute TSC.
class LLVM_ABI NewCPUIDRecord : public MetadataRecord {
  uint16_t CPUId = 0;
  uint64_t TSC = 0;
  friend class RecordInitializer;

public:
  /// Construct an empty new-CPU-ID record.
  NewCPUIDRecord()
      : MetadataRecord(RecordKind::RK_Metadata_NewCPUId,
                       MetadataType::NewCPUId) {}

  /// Construct a new-CPU-ID record for CPU \p C at absolute TSC \p T.
  /// \param C CPU identifier.
  /// \param T Absolute timestamp counter value.
  NewCPUIDRecord(uint16_t C, uint64_t T)
      : MetadataRecord(RecordKind::RK_Metadata_NewCPUId,
                       MetadataType::NewCPUId),
        CPUId(C), TSC(T) {}

  /// Return the CPU identifier.
  /// \return The CPU identifier.
  uint16_t cpuid() const { return CPUId; }

  /// Return the absolute timestamp counter value.
  /// \return The absolute timestamp counter value.
  uint64_t tsc() const { return TSC; }

  /// Apply visitor \p V to this new-CPU-ID record.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  Error apply(RecordVisitor &V) override;

  /// Return true if \p R is a NewCPUIDRecord.
  /// \param R Record to test.
  /// \return True if \p R is a NewCPUIDRecord.
  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_NewCPUId;
  }
};

/// Metadata record establishing a new absolute TSC base after a wrap.
class LLVM_ABI TSCWrapRecord : public MetadataRecord {
  uint64_t BaseTSC = 0;
  friend class RecordInitializer;

public:
  /// Construct an empty TSC-wrap record.
  TSCWrapRecord()
      : MetadataRecord(RecordKind::RK_Metadata_TSCWrap, MetadataType::TSCWrap) {
  }

  /// Construct a TSC-wrap record with absolute base TSC \p B.
  /// \param B Absolute base timestamp counter value.
  explicit TSCWrapRecord(uint64_t B)
      : MetadataRecord(RecordKind::RK_Metadata_TSCWrap, MetadataType::TSCWrap),
        BaseTSC(B) {}

  /// Return the absolute base timestamp counter value.
  /// \return The absolute base timestamp counter value.
  uint64_t tsc() const { return BaseTSC; }

  /// Apply visitor \p V to this TSC-wrap record.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  Error apply(RecordVisitor &V) override;

  /// Return true if \p R is a TSCWrapRecord.
  /// \param R Record to test.
  /// \return True if \p R is a TSCWrapRecord.
  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_TSCWrap;
  }
};

/// Pre-v5 metadata record for a user-defined custom event.
class LLVM_ABI CustomEventRecord : public MetadataRecord {
  int32_t Size = 0;
  uint64_t TSC = 0;
  uint16_t CPU = 0;
  std::string Data{};
  friend class RecordInitializer;

public:
  /// Construct an empty custom-event record.
  CustomEventRecord()
      : MetadataRecord(RecordKind::RK_Metadata_CustomEvent,
                       MetadataType::CustomEvent) {}

  /// Construct a custom-event record with size \p S, TSC \p T, CPU \p C, and
  /// payload \p D.
  /// \param S Size of the event payload in bytes.
  /// \param T Absolute timestamp counter value.
  /// \param C CPU identifier where the event was recorded.
  /// \param D Event payload data.
  explicit CustomEventRecord(uint64_t S, uint64_t T, uint16_t C, std::string D)
      : MetadataRecord(RecordKind::RK_Metadata_CustomEvent,
                       MetadataType::CustomEvent),
        Size(S), TSC(T), CPU(C), Data(std::move(D)) {}

  /// Return the size of the event payload in bytes.
  /// \return The size of the event payload in bytes.
  int32_t size() const { return Size; }
  /// Return the absolute timestamp counter value.
  /// \return The absolute timestamp counter value.
  uint64_t tsc() const { return TSC; }
  /// Return the CPU identifier where the event was recorded.
  /// \return The CPU identifier where the event was recorded.
  uint16_t cpu() const { return CPU; }
  /// Return the event payload data.
  /// \return The event payload data.
  StringRef data() const { return Data; }

  /// Apply visitor \p V to this custom-event record.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  Error apply(RecordVisitor &V) override;

  /// Return true if \p R is a CustomEventRecord.
  /// \param R Record to test.
  /// \return True if \p R is a CustomEventRecord.
  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_CustomEvent;
  }
};

/// V5+ metadata record for a user-defined custom event with a relative TSC.
class LLVM_ABI CustomEventRecordV5 : public MetadataRecord {
  int32_t Size = 0;
  int32_t Delta = 0;
  std::string Data{};
  friend class RecordInitializer;

public:
  /// Construct an empty v5 custom-event record.
  CustomEventRecordV5()
      : MetadataRecord(RecordKind::RK_Metadata_CustomEventV5,
                       MetadataType::CustomEvent) {}

  /// Construct a v5 custom-event record with size \p S, TSC delta \p D, and
  /// payload \p P.
  /// \param S Size of the event payload in bytes.
  /// \param D Relative timestamp counter delta.
  /// \param P Event payload data.
  explicit CustomEventRecordV5(int32_t S, int32_t D, std::string P)
      : MetadataRecord(RecordKind::RK_Metadata_CustomEventV5,
                       MetadataType::CustomEvent),
        Size(S), Delta(D), Data(std::move(P)) {}

  /// Return the size of the event payload in bytes.
  /// \return The size of the event payload in bytes.
  int32_t size() const { return Size; }
  /// Return the relative timestamp counter delta.
  /// \return The relative timestamp counter delta.
  int32_t delta() const { return Delta; }
  /// Return the event payload data.
  /// \return The event payload data.
  StringRef data() const { return Data; }

  /// Apply visitor \p V to this v5 custom-event record.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  Error apply(RecordVisitor &V) override;

  /// Return true if \p R is a CustomEventRecordV5.
  /// \param R Record to test.
  /// \return True if \p R is a CustomEventRecordV5.
  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_CustomEventV5;
  }
};

/// Metadata record for a typed user event with a type tag and payload.
class LLVM_ABI TypedEventRecord : public MetadataRecord {
  int32_t Size = 0;
  int32_t Delta = 0;
  uint16_t EventType = 0;
  std::string Data{};
  friend class RecordInitializer;

public:
  /// Construct an empty typed-event record.
  TypedEventRecord()
      : MetadataRecord(RecordKind::RK_Metadata_TypedEvent,
                       MetadataType::TypedEvent) {}

  /// Construct a typed-event record with size \p S, TSC delta \p D, event type
  /// \p E, and payload \p P.
  /// \param S Size of the event payload in bytes.
  /// \param D Relative timestamp counter delta.
  /// \param E Event type tag.
  /// \param P Event payload data.
  explicit TypedEventRecord(int32_t S, int32_t D, uint16_t E, std::string P)
      : MetadataRecord(RecordKind::RK_Metadata_TypedEvent,
                       MetadataType::TypedEvent),
        Size(S), Delta(D), Data(std::move(P)) {}

  /// Return the size of the event payload in bytes.
  /// \return The size of the event payload in bytes.
  int32_t size() const { return Size; }
  /// Return the relative timestamp counter delta.
  /// \return The relative timestamp counter delta.
  int32_t delta() const { return Delta; }
  /// Return the event type tag.
  /// \return The event type tag.
  uint16_t eventType() const { return EventType; }
  /// Return the event payload data.
  /// \return The event payload data.
  StringRef data() const { return Data; }

  /// Apply visitor \p V to this typed-event record.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  Error apply(RecordVisitor &V) override;

  /// Return true if \p R is a TypedEventRecord.
  /// \param R Record to test.
  /// \return True if \p R is a TypedEventRecord.
  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_TypedEvent;
  }
};

/// Metadata record carrying one argument value for a function enter-with-args.
class LLVM_ABI CallArgRecord : public MetadataRecord {
  uint64_t Arg = 0;
  friend class RecordInitializer;

public:
  /// Construct an empty call-argument record.
  CallArgRecord()
      : MetadataRecord(RecordKind::RK_Metadata_CallArg, MetadataType::CallArg) {
  }

  /// Construct a call-argument record holding value \p A.
  /// \param A Argument value.
  explicit CallArgRecord(uint64_t A)
      : MetadataRecord(RecordKind::RK_Metadata_CallArg, MetadataType::CallArg),
        Arg(A) {}

  /// Return the argument value.
  /// \return The argument value.
  uint64_t arg() const { return Arg; }

  /// Apply visitor \p V to this call-argument record.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  Error apply(RecordVisitor &V) override;

  /// Return true if \p R is a CallArgRecord.
  /// \param R Record to test.
  /// \return True if \p R is a CallArgRecord.
  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_CallArg;
  }
};

/// Metadata record giving the process ID for a following FDR buffer.
class LLVM_ABI PIDRecord : public MetadataRecord {
  int32_t PID = 0;
  friend class RecordInitializer;

public:
  /// Construct an empty PID record.
  PIDRecord()
      : MetadataRecord(RecordKind::RK_Metadata_PIDEntry,
                       MetadataType::PIDEntry) {}

  /// Construct a PID record for process ID \p P.
  /// \param P Process identifier.
  explicit PIDRecord(int32_t P)
      : MetadataRecord(RecordKind::RK_Metadata_PIDEntry,
                       MetadataType::PIDEntry),
        PID(P) {}

  /// Return the process identifier.
  /// \return The process identifier.
  int32_t pid() const { return PID; }

  /// Apply visitor \p V to this PID record.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  Error apply(RecordVisitor &V) override;

  /// Return true if \p R is a PIDRecord.
  /// \param R Record to test.
  /// \return True if \p R is a PIDRecord.
  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_PIDEntry;
  }
};

/// Metadata record marking the start of a new per-thread FDR buffer.
class LLVM_ABI NewBufferRecord : public MetadataRecord {
  int32_t TID = 0;
  friend class RecordInitializer;

public:
  /// Construct an empty new-buffer record.
  NewBufferRecord()
      : MetadataRecord(RecordKind::RK_Metadata_NewBuffer,
                       MetadataType::NewBuffer) {}

  /// Construct a new-buffer record for thread ID \p T.
  /// \param T Thread identifier.
  explicit NewBufferRecord(int32_t T)
      : MetadataRecord(RecordKind::RK_Metadata_NewBuffer,
                       MetadataType::NewBuffer),
        TID(T) {}

  /// Return the thread identifier for this buffer.
  /// \return The thread identifier for this buffer.
  int32_t tid() const { return TID; }

  /// Apply visitor \p V to this new-buffer record.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  Error apply(RecordVisitor &V) override;

  /// Return true if \p R is a NewBufferRecord.
  /// \param R Record to test.
  /// \return True if \p R is a NewBufferRecord.
  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_NewBuffer;
  }
};

/// Metadata record marking the end of the current FDR buffer.
class LLVM_ABI EndBufferRecord : public MetadataRecord {
public:
  /// Construct an end-of-buffer record.
  EndBufferRecord()
      : MetadataRecord(RecordKind::RK_Metadata_EndOfBuffer,
                       MetadataType::EndOfBuffer) {}

  /// Apply visitor \p V to this end-of-buffer record.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  Error apply(RecordVisitor &V) override;

  /// Return true if \p R is an EndBufferRecord.
  /// \param R Record to test.
  /// \return True if \p R is an EndBufferRecord.
  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_EndOfBuffer;
  }
};

/// Concrete FDR record for a function enter, exit, or related event.
///
/// A function record is a concrete record type which has a number of common
/// properties.
class LLVM_ABI FunctionRecord : public Record {
  RecordTypes Kind;
  int32_t FuncId = 0;
  uint32_t Delta = 0;
  friend class RecordInitializer;

  static constexpr unsigned kFunctionRecordSize = 8;

public:
  /// Construct an empty function record.
  FunctionRecord() : Record(RecordKind::RK_Function) {}

  /// Construct a function record of type \p K for function \p F with TSC delta
  /// \p D.
  /// \param K Function record subtype (enter, exit, and so on).
  /// \param F Function identifier.
  /// \param D Relative timestamp counter delta.
  explicit FunctionRecord(RecordTypes K, int32_t F, uint32_t D)
      : Record(RecordKind::RK_Function), Kind(K), FuncId(F), Delta(D) {}

  /// Return the function record subtype.
  /// \return The function record subtype.
  RecordTypes recordType() const { return Kind; }
  /// Return the function identifier.
  /// \return The function identifier.
  int32_t functionId() const { return FuncId; }
  /// Return the relative timestamp counter delta.
  /// \return The relative timestamp counter delta.
  uint32_t delta() const { return Delta; }

  /// Apply visitor \p V to this function record.
  /// \param V Visitor to apply.
  /// \return Success, or an error if applying the visitor failed.
  Error apply(RecordVisitor &V) override;

  /// Return true if \p R is a FunctionRecord.
  /// \param R Record to test.
  /// \return True if \p R is a FunctionRecord.
  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Function;
  }
};

/// Abstract visitor for processing concrete FDR record types.
class RecordVisitor {
public:
  /// Destroy the visitor.
  virtual ~RecordVisitor() = default;

  /// Visit a buffer-extents metadata record.
  /// \param R Buffer extents record being visited.
  /// \return Success, or an error if visiting the record failed.
  virtual Error visit(BufferExtents &R) = 0;
  /// Visit a wall-clock metadata record.
  /// \param R Wall-clock record being visited.
  /// \return Success, or an error if visiting the record failed.
  virtual Error visit(WallclockRecord &R) = 0;
  /// Visit a new-CPU-ID metadata record.
  /// \param R New CPU ID record being visited.
  /// \return Success, or an error if visiting the record failed.
  virtual Error visit(NewCPUIDRecord &R) = 0;
  /// Visit a TSC-wrap metadata record.
  /// \param R TSC wrap record being visited.
  /// \return Success, or an error if visiting the record failed.
  virtual Error visit(TSCWrapRecord &R) = 0;
  /// Visit a custom-event metadata record.
  /// \param R Custom event record being visited.
  /// \return Success, or an error if visiting the record failed.
  virtual Error visit(CustomEventRecord &R) = 0;
  /// Visit a call-argument metadata record.
  /// \param R Call argument record being visited.
  /// \return Success, or an error if visiting the record failed.
  virtual Error visit(CallArgRecord &R) = 0;
  /// Visit a PID metadata record.
  /// \param R PID record being visited.
  /// \return Success, or an error if visiting the record failed.
  virtual Error visit(PIDRecord &R) = 0;
  /// Visit a new-buffer metadata record.
  /// \param R New buffer record being visited.
  /// \return Success, or an error if visiting the record failed.
  virtual Error visit(NewBufferRecord &R) = 0;
  /// Visit an end-of-buffer metadata record.
  /// \param R End-of-buffer record being visited.
  /// \return Success, or an error if visiting the record failed.
  virtual Error visit(EndBufferRecord &R) = 0;
  /// Visit a function record.
  /// \param R Function record being visited.
  /// \return Success, or an error if visiting the record failed.
  virtual Error visit(FunctionRecord &R) = 0;
  /// Visit a v5 custom-event metadata record.
  /// \param R V5 custom event record being visited.
  /// \return Success, or an error if visiting the record failed.
  virtual Error visit(CustomEventRecordV5 &R) = 0;
  /// Visit a typed-event metadata record.
  /// \param R Typed event record being visited.
  /// \return Success, or an error if visiting the record failed.
  virtual Error visit(TypedEventRecord &R) = 0;
};

/// RecordVisitor that initializes FDR records by reading from a DataExtractor.
class LLVM_ABI RecordInitializer : public RecordVisitor {
  DataExtractor &E;
  uint64_t &OffsetPtr;
  uint16_t Version;

public:
  /// Default FDR log version used when none is specified.
  static constexpr uint16_t DefaultVersion = 5u;

  /// Construct an initializer reading from \p DE at \p OP with version \p V.
  /// \param DE Data extractor providing the FDR log bytes.
  /// \param OP Byte offset into \p DE; advanced as records are read.
  /// \param V FDR log format version.
  explicit RecordInitializer(DataExtractor &DE, uint64_t &OP, uint16_t V)
      : E(DE), OffsetPtr(OP), Version(V) {}

  /// Construct an initializer reading from \p DE at \p OP using DefaultVersion.
  /// \param DE Data extractor providing the FDR log bytes.
  /// \param OP Byte offset into \p DE; advanced as records are read.
  explicit RecordInitializer(DataExtractor &DE, uint64_t &OP)
      : RecordInitializer(DE, OP, DefaultVersion) {}

  /// Initialize \p R by reading a buffer-extents record from the extractor.
  /// \param R Buffer extents record to populate.
  /// \return Success, or an error if reading the record failed.
  Error visit(BufferExtents &R) override;
  /// Initialize \p R by reading a wall-clock record from the extractor.
  /// \param R Wall-clock record to populate.
  /// \return Success, or an error if reading the record failed.
  Error visit(WallclockRecord &R) override;
  /// Initialize \p R by reading a new-CPU-ID record from the extractor.
  /// \param R New CPU ID record to populate.
  /// \return Success, or an error if reading the record failed.
  Error visit(NewCPUIDRecord &R) override;
  /// Initialize \p R by reading a TSC-wrap record from the extractor.
  /// \param R TSC wrap record to populate.
  /// \return Success, or an error if reading the record failed.
  Error visit(TSCWrapRecord &R) override;
  /// Initialize \p R by reading a custom-event record from the extractor.
  /// \param R Custom event record to populate.
  /// \return Success, or an error if reading the record failed.
  Error visit(CustomEventRecord &R) override;
  /// Initialize \p R by reading a call-argument record from the extractor.
  /// \param R Call argument record to populate.
  /// \return Success, or an error if reading the record failed.
  Error visit(CallArgRecord &R) override;
  /// Initialize \p R by reading a PID record from the extractor.
  /// \param R PID record to populate.
  /// \return Success, or an error if reading the record failed.
  Error visit(PIDRecord &R) override;
  /// Initialize \p R by reading a new-buffer record from the extractor.
  /// \param R New buffer record to populate.
  /// \return Success, or an error if reading the record failed.
  Error visit(NewBufferRecord &R) override;
  /// Initialize \p R by reading an end-of-buffer record from the extractor.
  /// \param R End-of-buffer record to populate.
  /// \return Success, or an error if reading the record failed.
  Error visit(EndBufferRecord &R) override;
  /// Initialize \p R by reading a function record from the extractor.
  /// \param R Function record to populate.
  /// \return Success, or an error if reading the record failed.
  Error visit(FunctionRecord &R) override;
  /// Initialize \p R by reading a v5 custom-event record from the extractor.
  /// \param R V5 custom event record to populate.
  /// \return Success, or an error if reading the record failed.
  Error visit(CustomEventRecordV5 &R) override;
  /// Initialize \p R by reading a typed-event record from the extractor.
  /// \param R Typed event record to populate.
  /// \return Success, or an error if reading the record failed.
  Error visit(TypedEventRecord &R) override;
};

} // namespace llvm::xray

#endif // LLVM_XRAY_FDRRECORDS_H
