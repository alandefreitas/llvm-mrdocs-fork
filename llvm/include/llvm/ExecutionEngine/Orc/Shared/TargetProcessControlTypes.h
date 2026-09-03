//===--- TargetProcessControlTypes.h -- Shared Core/TPC types ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TargetProcessControl types that are used by both the Orc and
// OrcTargetProcess libraries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_TARGETPROCESSCONTROLTYPES_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_TARGETPROCESSCONTROLTYPES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Shared/AllocationActions.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/Orc/Shared/MemoryFlags.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimplePackedSerialization.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/Support/Memory.h"

#include <vector>

namespace llvm {
namespace orc {

/// Types shared between ORC and the target-process control layer.
namespace tpctypes {

/// Memory protection and lifetime policy for a remote allocation.
struct RemoteAllocGroup {
  /// Construct a remote alloc group with no protection flags.
  RemoteAllocGroup() = default;
  /// Construct a remote alloc group with the given protection flags.
  /// @param Prot Memory protection flags for the allocation.
  RemoteAllocGroup(MemProt Prot) : Prot(Prot) {}
  /// Construct a remote alloc group with protection and finalize lifetime.
  /// @param Prot Memory protection flags for the allocation.
  /// @param FinalizeLifetime Whether the allocation uses finalize lifetime.
  RemoteAllocGroup(MemProt Prot, bool FinalizeLifetime)
      : Prot(Prot), FinalizeLifetime(FinalizeLifetime) {}
  /// Construct a remote alloc group from a local AllocGroup.
  /// @param AG Allocation group whose protection and lifetime are copied.
  RemoteAllocGroup(const AllocGroup &AG) : Prot(AG.getMemProt()) {
    assert(AG.getMemLifetime() != orc::MemLifetime::NoAlloc &&
           "Cannot use no-alloc memory in a remote alloc request");
    FinalizeLifetime = AG.getMemLifetime() == orc::MemLifetime::Finalize;
  }

  /// Memory protection flags for the remote allocation.
  MemProt Prot;
  /// True if the allocation uses finalize lifetime rather than standard.
  bool FinalizeLifetime = false;
};

/// Request to finalize a single memory segment in the target process.
struct SegFinalizeRequest {
  /// Allocation group describing protection and lifetime for the segment.
  RemoteAllocGroup RAG;
  /// Base address of the segment in the target process.
  ExecutorAddr Addr;
  /// Size of the segment in bytes.
  uint64_t Size;
  /// Content to write into the segment before finalization.
  ArrayRef<char> Content;
};

/// Request to finalize one or more segments and run allocation actions.
struct FinalizeRequest {
  /// Segment finalize requests to apply.
  std::vector<SegFinalizeRequest> Segments;
  /// Allocation actions to run at finalize and dealloc time.
  shared::AllocActions Actions;
};

/// Request to finalize a shared-memory segment in the target process.
struct SharedMemorySegFinalizeRequest {
  /// Allocation group describing protection and lifetime for the segment.
  RemoteAllocGroup RAG;
  /// Base address of the shared-memory segment in the target process.
  ExecutorAddr Addr;
  /// Size of the shared-memory segment in bytes.
  uint64_t Size;
};

/// Request to finalize shared-memory segments and run allocation actions.
struct SharedMemoryFinalizeRequest {
  /// Shared-memory segment finalize requests to apply.
  std::vector<SharedMemorySegFinalizeRequest> Segments;
  /// Allocation actions to run at finalize and dealloc time.
  shared::AllocActions Actions;
};

/// Describes a write of an integer value to target-process memory.
template <typename T> struct UIntWrite {
  /// Construct an empty write with a zero value.
  UIntWrite() = default;
  /// Construct a write of \p Value to \p Addr.
  /// @param Addr Destination address in the target process.
  /// @param Value Integer value to write.
  UIntWrite(ExecutorAddr Addr, T Value) : Addr(Addr), Value(Value) {}

  /// Destination address in the target process.
  ExecutorAddr Addr;
  /// Integer value to write at \c Addr.
  T Value = 0;
};

/// Describes a write to a uint8_t.
using UInt8Write = UIntWrite<uint8_t>;

/// Describes a write to a uint16_t.
using UInt16Write = UIntWrite<uint16_t>;

/// Describes a write to a uint32_t.
using UInt32Write = UIntWrite<uint32_t>;

/// Describes a write to a uint64_t.
using UInt64Write = UIntWrite<uint64_t>;

/// Describes a write to a buffer.
/// For use with TargetProcessControl::MemoryAccess objects.
struct BufferWrite {
  /// Construct an empty buffer write.
  BufferWrite() = default;
  /// Construct a write of \p Buffer to \p Addr.
  /// @param Addr Destination address in the target process.
  /// @param Buffer Bytes to write.
  BufferWrite(ExecutorAddr Addr, ArrayRef<char> Buffer)
      : Addr(Addr), Buffer(Buffer) {}

  /// Destination address in the target process.
  ExecutorAddr Addr;
  /// Bytes to write at \c Addr.
  ArrayRef<char> Buffer;
};

/// Describes a write to a pointer.
/// For use with TargetProcessControl::MemoryAccess objects.
struct PointerWrite {
  /// Construct an empty pointer write.
  PointerWrite() = default;
  /// Construct a write of pointer value \p Value to \p Addr.
  /// @param Addr Destination address in the target process.
  /// @param Value Pointer value to write.
  PointerWrite(ExecutorAddr Addr, ExecutorAddr Value)
      : Addr(Addr), Value(Value) {}

  /// Destination address in the target process.
  ExecutorAddr Addr;
  /// Pointer value to write at \c Addr.
  ExecutorAddr Value;
};

/// A handle used to represent a loaded dylib in the target process.
using DylibHandle = ExecutorAddr;

/// A handle used to reference the resolver associated with a loaded
///  dylib in the target process.
using ResolverHandle = ExecutorAddr;

/// Result of a symbol lookup in the target process.
using LookupResult = std::vector<std::optional<ExecutorAddr>>;

} // end namespace tpctypes

namespace shared {

/// SPS tag type for RemoteAllocGroup.
class SPSRemoteAllocGroup;

/// SPS tag type for SegFinalizeRequest.
using SPSSegFinalizeRequest =
    SPSTuple<SPSRemoteAllocGroup, SPSExecutorAddr, uint64_t, SPSSequence<char>>;

/// SPS tag type for FinalizeRequest.
using SPSFinalizeRequest = SPSTuple<SPSSequence<SPSSegFinalizeRequest>,
                                    SPSSequence<SPSAllocActionCallPair>>;

/// SPS tag type for SharedMemorySegFinalizeRequest.
using SPSSharedMemorySegFinalizeRequest =
    SPSTuple<SPSRemoteAllocGroup, SPSExecutorAddr, uint64_t>;

/// SPS tag type for SharedMemoryFinalizeRequest.
using SPSSharedMemoryFinalizeRequest =
    SPSTuple<SPSSequence<SPSSharedMemorySegFinalizeRequest>,
             SPSSequence<SPSAllocActionCallPair>>;

/// SPS tag type for an integer memory-access write of type \c T.
template <typename T>
using SPSMemoryAccessUIntWrite = SPSTuple<SPSExecutorAddr, T>;

/// SPS tag type for a uint8_t memory-access write.
using SPSMemoryAccessUInt8Write = SPSMemoryAccessUIntWrite<uint8_t>;
/// SPS tag type for a uint16_t memory-access write.
using SPSMemoryAccessUInt16Write = SPSMemoryAccessUIntWrite<uint16_t>;
/// SPS tag type for a uint32_t memory-access write.
using SPSMemoryAccessUInt32Write = SPSMemoryAccessUIntWrite<uint32_t>;
/// SPS tag type for a uint64_t memory-access write.
using SPSMemoryAccessUInt64Write = SPSMemoryAccessUIntWrite<uint64_t>;

/// SPS tag type for a buffer memory-access write.
using SPSMemoryAccessBufferWrite = SPSTuple<SPSExecutorAddr, SPSSequence<char>>;
/// SPS tag type for a pointer memory-access write.
using SPSMemoryAccessPointerWrite = SPSTuple<SPSExecutorAddr, SPSExecutorAddr>;

/// SPS serializer for RemoteAllocGroup.
template <>
class SPSSerializationTraits<SPSRemoteAllocGroup, tpctypes::RemoteAllocGroup> {
  enum WireBits {
    ReadBit = 1 << 0,
    WriteBit = 1 << 1,
    ExecBit = 1 << 2,
    FinalizeBit = 1 << 3
  };

public:
  /// Return the serialized size of \p RAG.
  /// @param RAG Remote alloc group to measure.
  /// @return Number of bytes needed to serialize \p RAG.
  static size_t size(const tpctypes::RemoteAllocGroup &RAG) {
    // All AllocGroup values encode to the same size.
    return SPSArgList<uint8_t>::size(uint8_t(0));
  }

  /// Serialize \p RAG into \p OB.
  /// @param OB Output buffer.
  /// @param RAG Remote alloc group to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB,
                        const tpctypes::RemoteAllocGroup &RAG) {
    uint8_t WireValue = 0;
    if ((RAG.Prot & MemProt::Read) != MemProt::None)
      WireValue |= ReadBit;
    if ((RAG.Prot & MemProt::Write) != MemProt::None)
      WireValue |= WriteBit;
    if ((RAG.Prot & MemProt::Exec) != MemProt::None)
      WireValue |= ExecBit;
    if (RAG.FinalizeLifetime)
      WireValue |= FinalizeBit;
    return SPSArgList<uint8_t>::serialize(OB, WireValue);
  }

  /// Deserialize a RemoteAllocGroup from \p IB into \p RAG.
  /// @param IB Input buffer.
  /// @param RAG Destination remote alloc group.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB, tpctypes::RemoteAllocGroup &RAG) {
    uint8_t Val;
    if (!SPSArgList<uint8_t>::deserialize(IB, Val))
      return false;
    MemProt MP = MemProt::None;
    if (Val & ReadBit)
      MP |= MemProt::Read;
    if (Val & WriteBit)
      MP |= MemProt::Write;
    if (Val & ExecBit)
      MP |= MemProt::Exec;
    bool FinalizeLifetime = (Val & FinalizeBit) ? true : false;
    RAG = {MP, FinalizeLifetime};
    return true;
  }
};

/// SPS serializer for SegFinalizeRequest.
template <>
class SPSSerializationTraits<SPSSegFinalizeRequest,
                             tpctypes::SegFinalizeRequest> {
  using SFRAL = SPSSegFinalizeRequest::AsArgList;

public:
  /// Return the serialized size of \p SFR.
  /// @param SFR Segment finalize request to measure.
  /// @return Number of bytes needed to serialize \p SFR.
  static size_t size(const tpctypes::SegFinalizeRequest &SFR) {
    return SFRAL::size(SFR.RAG, SFR.Addr, SFR.Size, SFR.Content);
  }

  /// Serialize \p SFR into \p OB.
  /// @param OB Output buffer.
  /// @param SFR Segment finalize request to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB,
                        const tpctypes::SegFinalizeRequest &SFR) {
    return SFRAL::serialize(OB, SFR.RAG, SFR.Addr, SFR.Size, SFR.Content);
  }

  /// Deserialize a SegFinalizeRequest from \p IB into \p SFR.
  /// @param IB Input buffer.
  /// @param SFR Destination segment finalize request.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB,
                          tpctypes::SegFinalizeRequest &SFR) {
    return SFRAL::deserialize(IB, SFR.RAG, SFR.Addr, SFR.Size, SFR.Content);
  }
};

/// SPS serializer for FinalizeRequest.
template <>
class SPSSerializationTraits<SPSFinalizeRequest, tpctypes::FinalizeRequest> {
  using FRAL = SPSFinalizeRequest::AsArgList;

public:
  /// Return the serialized size of \p FR.
  /// @param FR Finalize request to measure.
  /// @return Number of bytes needed to serialize \p FR.
  static size_t size(const tpctypes::FinalizeRequest &FR) {
    return FRAL::size(FR.Segments, FR.Actions);
  }

  /// Serialize \p FR into \p OB.
  /// @param OB Output buffer.
  /// @param FR Finalize request to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB,
                        const tpctypes::FinalizeRequest &FR) {
    return FRAL::serialize(OB, FR.Segments, FR.Actions);
  }

  /// Deserialize a FinalizeRequest from \p IB into \p FR.
  /// @param IB Input buffer.
  /// @param FR Destination finalize request.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB, tpctypes::FinalizeRequest &FR) {
    return FRAL::deserialize(IB, FR.Segments, FR.Actions);
  }
};

/// SPS serializer for SharedMemorySegFinalizeRequest.
template <>
class SPSSerializationTraits<SPSSharedMemorySegFinalizeRequest,
                             tpctypes::SharedMemorySegFinalizeRequest> {
  using SFRAL = SPSSharedMemorySegFinalizeRequest::AsArgList;

public:
  /// Return the serialized size of \p SFR.
  /// @param SFR Shared-memory segment finalize request to measure.
  /// @return Number of bytes needed to serialize \p SFR.
  static size_t size(const tpctypes::SharedMemorySegFinalizeRequest &SFR) {
    return SFRAL::size(SFR.RAG, SFR.Addr, SFR.Size);
  }

  /// Serialize \p SFR into \p OB.
  /// @param OB Output buffer.
  /// @param SFR Shared-memory segment finalize request to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB,
                        const tpctypes::SharedMemorySegFinalizeRequest &SFR) {
    return SFRAL::serialize(OB, SFR.RAG, SFR.Addr, SFR.Size);
  }

  /// Deserialize a SharedMemorySegFinalizeRequest from \p IB into \p SFR.
  /// @param IB Input buffer.
  /// @param SFR Destination shared-memory segment finalize request.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB,
                          tpctypes::SharedMemorySegFinalizeRequest &SFR) {
    return SFRAL::deserialize(IB, SFR.RAG, SFR.Addr, SFR.Size);
  }
};

/// SPS serializer for SharedMemoryFinalizeRequest.
template <>
class SPSSerializationTraits<SPSSharedMemoryFinalizeRequest,
                             tpctypes::SharedMemoryFinalizeRequest> {
  using FRAL = SPSSharedMemoryFinalizeRequest::AsArgList;

public:
  /// Return the serialized size of \p FR.
  /// @param FR Shared-memory finalize request to measure.
  /// @return Number of bytes needed to serialize \p FR.
  static size_t size(const tpctypes::SharedMemoryFinalizeRequest &FR) {
    return FRAL::size(FR.Segments, FR.Actions);
  }

  /// Serialize \p FR into \p OB.
  /// @param OB Output buffer.
  /// @param FR Shared-memory finalize request to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB,
                        const tpctypes::SharedMemoryFinalizeRequest &FR) {
    return FRAL::serialize(OB, FR.Segments, FR.Actions);
  }

  /// Deserialize a SharedMemoryFinalizeRequest from \p IB into \p FR.
  /// @param IB Input buffer.
  /// @param FR Destination shared-memory finalize request.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB,
                          tpctypes::SharedMemoryFinalizeRequest &FR) {
    return FRAL::deserialize(IB, FR.Segments, FR.Actions);
  }
};

/// SPS serializer for UIntWrite.
template <typename T>
class SPSSerializationTraits<SPSMemoryAccessUIntWrite<T>,
                             tpctypes::UIntWrite<T>> {
public:
  /// Return the serialized size of \p W.
  /// @param W Integer write to measure.
  /// @return Number of bytes needed to serialize \p W.
  static size_t size(const tpctypes::UIntWrite<T> &W) {
    return SPSTuple<SPSExecutorAddr, T>::AsArgList::size(W.Addr, W.Value);
  }

  /// Serialize \p W into \p OB.
  /// @param OB Output buffer.
  /// @param W Integer write to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB, const tpctypes::UIntWrite<T> &W) {
    return SPSTuple<SPSExecutorAddr, T>::AsArgList::serialize(OB, W.Addr,
                                                              W.Value);
  }

  /// Deserialize a UIntWrite from \p IB into \p W.
  /// @param IB Input buffer.
  /// @param W Destination integer write.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB, tpctypes::UIntWrite<T> &W) {
    return SPSTuple<SPSExecutorAddr, T>::AsArgList::deserialize(IB, W.Addr,
                                                                W.Value);
  }
};

/// SPS serializer for BufferWrite.
template <>
class SPSSerializationTraits<SPSMemoryAccessBufferWrite,
                             tpctypes::BufferWrite> {
public:
  /// Return the serialized size of \p W.
  /// @param W Buffer write to measure.
  /// @return Number of bytes needed to serialize \p W.
  static size_t size(const tpctypes::BufferWrite &W) {
    return SPSTuple<SPSExecutorAddr, SPSSequence<char>>::AsArgList::size(
        W.Addr, W.Buffer);
  }

  /// Serialize \p W into \p OB.
  /// @param OB Output buffer.
  /// @param W Buffer write to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB, const tpctypes::BufferWrite &W) {
    return SPSTuple<SPSExecutorAddr, SPSSequence<char>>::AsArgList ::serialize(
        OB, W.Addr, W.Buffer);
  }

  /// Deserialize a BufferWrite from \p IB into \p W.
  /// @param IB Input buffer.
  /// @param W Destination buffer write.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB, tpctypes::BufferWrite &W) {
    return SPSTuple<SPSExecutorAddr,
                    SPSSequence<char>>::AsArgList ::deserialize(IB, W.Addr,
                                                                W.Buffer);
  }
};

/// SPS serializer for PointerWrite.
template <>
class SPSSerializationTraits<SPSMemoryAccessPointerWrite,
                             tpctypes::PointerWrite> {
public:
  /// Return the serialized size of \p W.
  /// @param W Pointer write to measure.
  /// @return Number of bytes needed to serialize \p W.
  static size_t size(const tpctypes::PointerWrite &W) {
    return SPSTuple<SPSExecutorAddr, SPSExecutorAddr>::AsArgList::size(W.Addr,
                                                                       W.Value);
  }

  /// Serialize \p W into \p OB.
  /// @param OB Output buffer.
  /// @param W Pointer write to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB, const tpctypes::PointerWrite &W) {
    return SPSTuple<SPSExecutorAddr, SPSExecutorAddr>::AsArgList::serialize(
        OB, W.Addr, W.Value);
  }

  /// Deserialize a PointerWrite from \p IB into \p W.
  /// @param IB Input buffer.
  /// @param W Destination pointer write.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB, tpctypes::PointerWrite &W) {
    return SPSTuple<SPSExecutorAddr, SPSExecutorAddr>::AsArgList::deserialize(
        IB, W.Addr, W.Value);
  }
};

} // end namespace shared
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_TARGETPROCESSCONTROLTYPES_H
