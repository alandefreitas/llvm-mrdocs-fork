//===--- PerfSharedStructs.h --- RPC Structs for perf support ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Structs and serialization to share perf-related information
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_PERFSHAREDSTRUCTS_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_PERFSHAREDSTRUCTS_H

#include "llvm/ExecutionEngine/Orc/Shared/SimplePackedSerialization.h"

namespace llvm {

namespace orc {

// The following are POD struct definitions from the perf jit specification

/// Record types defined by the Linux perf JIT interface.
enum class PerfJITRecordType {
  /// JIT code load notification.
  JIT_CODE_LOAD = 0,
  /// JIT code move notification (not emitted; code is not moved).
  JIT_CODE_MOVE = 1,
  /// JIT debug info notification.
  JIT_CODE_DEBUG_INFO = 2,
  /// JIT code close notification (not emitted; unnecessary).
  JIT_CODE_CLOSE = 3,
  /// JIT unwinding info notification (not emitted).
  JIT_CODE_UNWINDING_INFO = 4,

  /// Sentinel one past the last valid record type.
  JIT_CODE_MAX
};

/// Common prefix shared by all perf JIT records.
struct PerfJITRecordPrefix {
  /// Record type identifier (serialized as uint32_t).
  PerfJITRecordType Id;
  /// Total size of the record in bytes, including this prefix.
  uint32_t TotalSize;
};

/// Perf JIT record describing a newly loaded code region.
struct PerfJITCodeLoadRecord {
  /// Shared record prefix identifying type and total size.
  PerfJITRecordPrefix Prefix;

  /// Process ID that owns the loaded code.
  uint32_t Pid;
  /// Thread ID associated with the load.
  uint32_t Tid;
  /// Virtual memory address reported to perf.
  uint64_t Vma;
  /// Address of the loaded code.
  uint64_t CodeAddr;
  /// Size of the loaded code in bytes.
  uint64_t CodeSize;
  /// Monotonically increasing code index for this load.
  uint64_t CodeIndex;
  /// Symbolic name of the loaded code.
  std::string Name;
};

/// Single source-location entry within a perf JIT debug-info record.
struct PerfJITDebugEntry {
  /// Address corresponding to this source location.
  uint64_t Addr;
  /// Source line number starting at 1.
  uint32_t Lineno;
  /// Column discriminator; 0 is the default.
  uint32_t Discrim;
  /// Source file name for this entry.
  std::string Name;
};

/// Perf JIT record carrying debug line information for loaded code.
struct PerfJITDebugInfoRecord {
  /// Shared record prefix identifying type and total size.
  PerfJITRecordPrefix Prefix;

  /// Address of the code region described by \c Entries.
  uint64_t CodeAddr;
  /// Source-location entries for the code region.
  std::vector<PerfJITDebugEntry> Entries;
};

/// Perf JIT record describing EH-frame / unwinding information.
struct PerfJITCodeUnwindingInfoRecord {
  /// Shared record prefix identifying type and total size.
  PerfJITRecordPrefix Prefix;

  /// Total size of unwind data (EH frame header plus EH frame).
  uint64_t UnwindDataSize;
  /// Size of the EH frame header in bytes.
  uint64_t EHFrameHdrSize;
  /// Mapped size of the unwinding information.
  uint64_t MappedSize;
  /// Address of the EH frame header when provided out-of-band (else 0).
  ///
  /// Exactly one of \c EHFrameHdrAddr and \c EHFrameHdr carries data; the
  /// other is 0 or empty.
  uint64_t EHFrameHdrAddr;
  /// Inline EH frame header bytes when not provided via address (else empty).
  std::string EHFrameHdr;

  /// Address of the EH frame payload.
  ///
  /// The EH frame size is \c UnwindDataSize - \c EHFrameHdrSize.
  uint64_t EHFrameAddr;
};

/// Batch of perf JIT records sent together to minimize RPC traffic.
struct PerfJITRecordBatch {
  /// Debug-info records included in this batch.
  std::vector<PerfJITDebugInfoRecord> DebugInfoRecords;
  /// Code-load records included in this batch.
  std::vector<PerfJITCodeLoadRecord> CodeLoadRecords;
  /// Optional unwinding-info record; valid only when its record size is > 0.
  PerfJITCodeUnwindingInfoRecord UnwindingRecord;
};

// SPS traits for Records

namespace shared {

/// SPS tag type for PerfJITRecordPrefix.
using SPSPerfJITRecordPrefix = SPSTuple<uint32_t, uint32_t>;

/// SPS serializer for PerfJITRecordPrefix.
template <>
class SPSSerializationTraits<SPSPerfJITRecordPrefix, PerfJITRecordPrefix> {
public:
  /// Return the serialized size of \p Val.
  /// @param Val Record prefix to measure.
  /// @return Number of bytes needed to serialize \p Val.
  static size_t size(const PerfJITRecordPrefix &Val) {
    return SPSPerfJITRecordPrefix::AsArgList::size(
        static_cast<uint32_t>(Val.Id), Val.TotalSize);
  }
  /// Deserialize a PerfJITRecordPrefix from \p IB into \p Val.
  /// @param IB Input buffer.
  /// @param Val Destination record prefix.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB, PerfJITRecordPrefix &Val) {
    uint32_t Id;
    if (!SPSPerfJITRecordPrefix::AsArgList::deserialize(IB, Id, Val.TotalSize))
      return false;
    Val.Id = static_cast<PerfJITRecordType>(Id);
    return true;
  }
  /// Serialize \p Val into \p OB.
  /// @param OB Output buffer.
  /// @param Val Record prefix to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB, const PerfJITRecordPrefix &Val) {
    return SPSPerfJITRecordPrefix::AsArgList::serialize(
        OB, static_cast<uint32_t>(Val.Id), Val.TotalSize);
  }
};

/// SPS tag type for PerfJITCodeLoadRecord.
using SPSPerfJITCodeLoadRecord =
    SPSTuple<SPSPerfJITRecordPrefix, uint32_t, uint32_t, uint64_t, uint64_t,
             uint64_t, uint64_t, SPSString>;

/// SPS serializer for PerfJITCodeLoadRecord.
template <>
class SPSSerializationTraits<SPSPerfJITCodeLoadRecord, PerfJITCodeLoadRecord> {
public:
  /// Return the serialized size of \p Val.
  /// @param Val Code-load record to measure.
  /// @return Number of bytes needed to serialize \p Val.
  static size_t size(const PerfJITCodeLoadRecord &Val) {
    return SPSPerfJITCodeLoadRecord::AsArgList::size(
        Val.Prefix, Val.Pid, Val.Tid, Val.Vma, Val.CodeAddr, Val.CodeSize,
        Val.CodeIndex, Val.Name);
  }

  /// Deserialize a PerfJITCodeLoadRecord from \p IB into \p Val.
  /// @param IB Input buffer.
  /// @param Val Destination code-load record.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB, PerfJITCodeLoadRecord &Val) {
    return SPSPerfJITCodeLoadRecord::AsArgList::deserialize(
        IB, Val.Prefix, Val.Pid, Val.Tid, Val.Vma, Val.CodeAddr, Val.CodeSize,
        Val.CodeIndex, Val.Name);
  }

  /// Serialize \p Val into \p OB.
  /// @param OB Output buffer.
  /// @param Val Code-load record to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB, const PerfJITCodeLoadRecord &Val) {
    return SPSPerfJITCodeLoadRecord::AsArgList::serialize(
        OB, Val.Prefix, Val.Pid, Val.Tid, Val.Vma, Val.CodeAddr, Val.CodeSize,
        Val.CodeIndex, Val.Name);
  }
};

/// SPS tag type for PerfJITDebugEntry.
using SPSPerfJITDebugEntry = SPSTuple<uint64_t, uint32_t, uint32_t, SPSString>;

/// SPS serializer for PerfJITDebugEntry.
template <>
class SPSSerializationTraits<SPSPerfJITDebugEntry, PerfJITDebugEntry> {
public:
  /// Return the serialized size of \p Val.
  /// @param Val Debug entry to measure.
  /// @return Number of bytes needed to serialize \p Val.
  static size_t size(const PerfJITDebugEntry &Val) {
    return SPSPerfJITDebugEntry::AsArgList::size(Val.Addr, Val.Lineno,
                                                 Val.Discrim, Val.Name);
  }

  /// Deserialize a PerfJITDebugEntry from \p IB into \p Val.
  /// @param IB Input buffer.
  /// @param Val Destination debug entry.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB, PerfJITDebugEntry &Val) {
    return SPSPerfJITDebugEntry::AsArgList::deserialize(
        IB, Val.Addr, Val.Lineno, Val.Discrim, Val.Name);
  }

  /// Serialize \p Val into \p OB.
  /// @param OB Output buffer.
  /// @param Val Debug entry to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB, const PerfJITDebugEntry &Val) {
    return SPSPerfJITDebugEntry::AsArgList::serialize(OB, Val.Addr, Val.Lineno,
                                                      Val.Discrim, Val.Name);
  }
};

/// SPS tag type for PerfJITDebugInfoRecord.
using SPSPerfJITDebugInfoRecord = SPSTuple<SPSPerfJITRecordPrefix, uint64_t,
                                           SPSSequence<SPSPerfJITDebugEntry>>;

/// SPS serializer for PerfJITDebugInfoRecord.
template <>
class SPSSerializationTraits<SPSPerfJITDebugInfoRecord,
                             PerfJITDebugInfoRecord> {
public:
  /// Return the serialized size of \p Val.
  /// @param Val Debug-info record to measure.
  /// @return Number of bytes needed to serialize \p Val.
  static size_t size(const PerfJITDebugInfoRecord &Val) {
    return SPSPerfJITDebugInfoRecord::AsArgList::size(Val.Prefix, Val.CodeAddr,
                                                      Val.Entries);
  }
  /// Deserialize a PerfJITDebugInfoRecord from \p IB into \p Val.
  /// @param IB Input buffer.
  /// @param Val Destination debug-info record.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB, PerfJITDebugInfoRecord &Val) {
    return SPSPerfJITDebugInfoRecord::AsArgList::deserialize(
        IB, Val.Prefix, Val.CodeAddr, Val.Entries);
  }
  /// Serialize \p Val into \p OB.
  /// @param OB Output buffer.
  /// @param Val Debug-info record to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB,
                        const PerfJITDebugInfoRecord &Val) {
    return SPSPerfJITDebugInfoRecord::AsArgList::serialize(
        OB, Val.Prefix, Val.CodeAddr, Val.Entries);
  }
};

/// SPS tag type for PerfJITCodeUnwindingInfoRecord.
using SPSPerfJITCodeUnwindingInfoRecord =
    SPSTuple<SPSPerfJITRecordPrefix, uint64_t, uint64_t, uint64_t, uint64_t,
             SPSString, uint64_t>;
/// SPS serializer for PerfJITCodeUnwindingInfoRecord.
template <>
class SPSSerializationTraits<SPSPerfJITCodeUnwindingInfoRecord,
                             PerfJITCodeUnwindingInfoRecord> {
public:
  /// Return the serialized size of \p Val.
  /// @param Val Unwinding-info record to measure.
  /// @return Number of bytes needed to serialize \p Val.
  static size_t size(const PerfJITCodeUnwindingInfoRecord &Val) {
    return SPSPerfJITCodeUnwindingInfoRecord::AsArgList::size(
        Val.Prefix, Val.UnwindDataSize, Val.EHFrameHdrSize, Val.MappedSize,
        Val.EHFrameHdrAddr, Val.EHFrameHdr, Val.EHFrameAddr);
  }
  /// Deserialize a PerfJITCodeUnwindingInfoRecord from \p IB into \p Val.
  /// @param IB Input buffer.
  /// @param Val Destination unwinding-info record.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB,
                          PerfJITCodeUnwindingInfoRecord &Val) {
    return SPSPerfJITCodeUnwindingInfoRecord::AsArgList::deserialize(
        IB, Val.Prefix, Val.UnwindDataSize, Val.EHFrameHdrSize, Val.MappedSize,
        Val.EHFrameHdrAddr, Val.EHFrameHdr, Val.EHFrameAddr);
  }
  /// Serialize \p Val into \p OB.
  /// @param OB Output buffer.
  /// @param Val Unwinding-info record to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB,
                        const PerfJITCodeUnwindingInfoRecord &Val) {
    return SPSPerfJITCodeUnwindingInfoRecord::AsArgList::serialize(
        OB, Val.Prefix, Val.UnwindDataSize, Val.EHFrameHdrSize, Val.MappedSize,
        Val.EHFrameHdrAddr, Val.EHFrameHdr, Val.EHFrameAddr);
  }
};

/// SPS tag type for PerfJITRecordBatch.
using SPSPerfJITRecordBatch = SPSTuple<SPSSequence<SPSPerfJITCodeLoadRecord>,
                                       SPSSequence<SPSPerfJITDebugInfoRecord>,
                                       SPSPerfJITCodeUnwindingInfoRecord>;
/// SPS serializer for PerfJITRecordBatch.
template <>
class SPSSerializationTraits<SPSPerfJITRecordBatch, PerfJITRecordBatch> {
public:
  /// Return the serialized size of \p Val.
  /// @param Val Record batch to measure.
  /// @return Number of bytes needed to serialize \p Val.
  static size_t size(const PerfJITRecordBatch &Val) {
    return SPSPerfJITRecordBatch::AsArgList::size(
        Val.CodeLoadRecords, Val.DebugInfoRecords, Val.UnwindingRecord);
  }
  /// Deserialize a PerfJITRecordBatch from \p IB into \p Val.
  /// @param IB Input buffer.
  /// @param Val Destination record batch.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB, PerfJITRecordBatch &Val) {
    return SPSPerfJITRecordBatch::AsArgList::deserialize(
        IB, Val.CodeLoadRecords, Val.DebugInfoRecords, Val.UnwindingRecord);
  }
  /// Serialize \p Val into \p OB.
  /// @param OB Output buffer.
  /// @param Val Record batch to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB, const PerfJITRecordBatch &Val) {
    return SPSPerfJITRecordBatch::AsArgList::serialize(
        OB, Val.CodeLoadRecords, Val.DebugInfoRecords, Val.UnwindingRecord);
  }
};

} // namespace shared

} // namespace orc

} // namespace llvm

#endif
