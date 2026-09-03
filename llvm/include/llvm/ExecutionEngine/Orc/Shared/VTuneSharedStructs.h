//===-------------------- VTuneSharedStructs.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Structs and serialization to share VTune-related information
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_VTUNESHAREDSTRUCTS_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_VTUNESHAREDSTRUCTS_H

#include "ExecutorAddress.h"
#include <utility>
#include <vector>

namespace llvm {
namespace orc {

/// Line-number mapping for a VTune method (offset, line) pairs.
using VTuneLineTable = std::vector<std::pair<unsigned, unsigned>>;

/// VTune JIT method metadata shared with the profiling agent.
struct VTuneMethodInfo {
  /// Line-number table for this method.
  VTuneLineTable LineTable;
  /// Load address of the method in the executor.
  ExecutorAddr LoadAddr;
  /// Size in bytes of the loaded method.
  uint64_t LoadSize;
  /// Unique identifier for this method.
  uint64_t MethodID;
  /// 1-indexed string-table index of the method name; 0 means nullptr.
  uint32_t NameSI;
  /// 1-indexed string-table index of the class file name; 0 means nullptr.
  uint32_t ClassFileSI;
  /// 1-indexed string-table index of the source file name; 0 means nullptr.
  uint32_t SourceFileSI;
  /// 1-indexed method-table index of the parent; 0 means not inlined.
  uint32_t ParentMI;
};

/// Table of VTune method info records.
using VTuneMethodTable = std::vector<VTuneMethodInfo>;
/// String table referenced by VTune method info string indexes.
using VTuneStringTable = std::vector<std::string>;

/// Batch of VTune method records and their shared string table.
struct VTuneMethodBatch {
  /// Method info records included in this batch.
  VTuneMethodTable Methods;
  /// Shared string table for method name and file indexes.
  VTuneStringTable Strings;
};

/// Pairs of unloaded method IDs reported to VTune.
using VTuneUnloadedMethodIDs = SmallVector<std::pair<uint64_t, uint64_t>>;

namespace shared {

/// SPS tag type for VTuneLineTable.
using SPSVTuneLineTable = SPSSequence<SPSTuple<uint32_t, uint32_t>>;
/// SPS tag type for VTuneMethodInfo.
using SPSVTuneMethodInfo =
    SPSTuple<SPSVTuneLineTable, SPSExecutorAddr, uint64_t, uint64_t, uint32_t,
             uint32_t, uint32_t, uint32_t>;
/// SPS tag type for VTuneMethodTable.
using SPSVTuneMethodTable = SPSSequence<SPSVTuneMethodInfo>;
/// SPS tag type for VTuneStringTable.
using SPSVTuneStringTable = SPSSequence<SPSString>;
/// SPS tag type for VTuneMethodBatch.
using SPSVTuneMethodBatch = SPSTuple<SPSVTuneMethodTable, SPSVTuneStringTable>;
/// SPS tag type for VTuneUnloadedMethodIDs.
using SPSVTuneUnloadedMethodIDs = SPSSequence<SPSTuple<uint64_t, uint64_t>>;

/// SPS serializer for VTuneMethodInfo.
template <> class SPSSerializationTraits<SPSVTuneMethodInfo, VTuneMethodInfo> {
public:
  /// Return the serialized size of \p MI.
  /// @param MI Method info to measure.
  /// @return Number of bytes needed to serialize \p MI.
  static size_t size(const VTuneMethodInfo &MI) {
    return SPSVTuneMethodInfo::AsArgList::size(
        MI.LineTable, MI.LoadAddr, MI.LoadSize, MI.MethodID, MI.NameSI,
        MI.ClassFileSI, MI.SourceFileSI, MI.ParentMI);
  }

  /// Deserialize a VTuneMethodInfo from \p IB into \p MI.
  /// @param IB Input buffer.
  /// @param MI Destination method info.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB, VTuneMethodInfo &MI) {
    return SPSVTuneMethodInfo::AsArgList::deserialize(
        IB, MI.LineTable, MI.LoadAddr, MI.LoadSize, MI.MethodID, MI.NameSI,
        MI.ClassFileSI, MI.SourceFileSI, MI.ParentMI);
  }

  /// Serialize \p MI into \p OB.
  /// @param OB Output buffer.
  /// @param MI Method info to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB, const VTuneMethodInfo &MI) {
    return SPSVTuneMethodInfo::AsArgList::serialize(
        OB, MI.LineTable, MI.LoadAddr, MI.LoadSize, MI.MethodID, MI.NameSI,
        MI.ClassFileSI, MI.SourceFileSI, MI.ParentMI);
  }
};

/// SPS serializer for VTuneMethodBatch.
template <>
class SPSSerializationTraits<SPSVTuneMethodBatch, VTuneMethodBatch> {
public:
  /// Return the serialized size of \p MB.
  /// @param MB Method batch to measure.
  /// @return Number of bytes needed to serialize \p MB.
  static size_t size(const VTuneMethodBatch &MB) {
    return SPSVTuneMethodBatch::AsArgList::size(MB.Methods, MB.Strings);
  }

  /// Deserialize a VTuneMethodBatch from \p IB into \p MB.
  /// @param IB Input buffer.
  /// @param MB Destination method batch.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB, VTuneMethodBatch &MB) {
    return SPSVTuneMethodBatch::AsArgList::deserialize(IB, MB.Methods,
                                                       MB.Strings);
  }

  /// Serialize \p MB into \p OB.
  /// @param OB Output buffer.
  /// @param MB Method batch to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB, const VTuneMethodBatch &MB) {
    return SPSVTuneMethodBatch::AsArgList::serialize(OB, MB.Methods,
                                                     MB.Strings);
  }
};

} // end namespace shared
} // end namespace orc
} // end namespace llvm

#endif
