//===- IndexedMemProfData.h - MemProf format support ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements IndexedMemProfData, a data structure to hold MemProf
// in a space optimized format. It also provides utility methods for writing
// MemProf data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_INDEXEDMEMPROFDATA_H
#define LLVM_PROFILEDATA_INDEXEDMEMPROFDATA_H

#include "llvm/ProfileData/DataAccessProf.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/MemProf.h"
#include "llvm/Support/BLAKE3.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/HashBuilder.h"

namespace llvm {
namespace memprof {
class MemProfSummary;
/// Space-optimized container for indexed MemProf profile data.
struct IndexedMemProfData {
  /// Map from function GUID to indexed MemProf record.
  ///
  /// The lower 64 bits obtained from the md5 hash of the function name is used
  /// to index into the map.
  llvm::MapVector<GlobalValue::GUID, IndexedMemProfRecord> Records;

  /// Map from frame id to frame contents.
  ///
  /// The mappings are used to convert IndexedMemProfRecord to MemProfRecords
  /// with frame information inline.
  llvm::MapVector<FrameId, Frame> Frames;

  /// Map from call stack id to the sequence of frame ids in that call stack.
  llvm::MapVector<CallStackId, llvm::SmallVector<FrameId>> CallStacks;

  /// Insert \p F into Frames and return its FrameId.
  /// @param F Frame to insert.
  /// @return FrameId of the inserted (or existing) frame.
  FrameId addFrame(const Frame &F) {
    const FrameId Id = hashFrame(F);
    Frames.try_emplace(Id, F);
    return Id;
  }

  /// Insert call stack \p CS into CallStacks and return its CallStackId.
  /// @param CS Sequence of frame ids forming the call stack.
  /// @return CallStackId of the inserted (or existing) call stack.
  CallStackId addCallStack(ArrayRef<FrameId> CS) {
    CallStackId CSId = hashCallStack(CS);
    CallStacks.try_emplace(CSId, CS);
    return CSId;
  }

  /// Insert call stack \p CS into CallStacks by move and return its CallStackId.
  /// @param CS Sequence of frame ids forming the call stack; contents are moved.
  /// @return CallStackId of the inserted (or existing) call stack.
  CallStackId addCallStack(SmallVector<FrameId> &&CS) {
    CallStackId CSId = hashCallStack(CS);
    CallStacks.try_emplace(CSId, std::move(CS));
    return CSId;
  }

private:
  // Return a hash value based on the contents of the frame. Here we use a
  // cryptographic hash function to minimize the chance of hash collisions.  We
  // do persist FrameIds as part of memprof formats up to Version 2, inclusive.
  // However, the deserializer never calls this function; it uses FrameIds
  // merely as keys to look up Frames proper.
  FrameId hashFrame(const Frame &F) const {
    llvm::HashBuilder<llvm::TruncatedBLAKE3<8>, llvm::endianness::little>
        HashBuilder;
    HashBuilder.add(F.Function, F.LineOffset, F.Column, F.IsInlineFrame);
    llvm::BLAKE3Result<8> Hash = HashBuilder.final();
    FrameId Id;
    std::memcpy(&Id, Hash.data(), sizeof(Hash));
    return Id;
  }

  // Compute a CallStackId for a given call stack.
  CallStackId hashCallStack(ArrayRef<FrameId> CS) const {
    llvm::HashBuilder<llvm::TruncatedBLAKE3<8>, llvm::endianness::little>
        HashBuilder;
    for (FrameId F : CS)
      HashBuilder.add(F);
    llvm::BLAKE3Result<8> Hash = HashBuilder.final();
    CallStackId CSId;
    std::memcpy(&CSId, Hash.data(), sizeof(Hash));
    return CSId;
  }
};
} // namespace memprof

/// Write MemProf profile data to \p OS in the requested indexed format version.
/// @param OS Destination stream.
/// @param MemProfData Indexed MemProf records, frames, and call stacks to write.
/// @param MemProfVersionRequested Indexed format version to emit.
/// @param MemProfFullSchema Whether to serialize the full MemInfoBlock schema.
/// @param DataAccessProfileData Optional data-access profile (Version 4+).
/// @param MemProfSum Optional MemProf summary (Version 4+).
/// @return Success, or an error if writing fails.
LLVM_ABI Error writeMemProf(
    ProfOStream &OS, memprof::IndexedMemProfData &MemProfData,
    memprof::IndexedVersion MemProfVersionRequested, bool MemProfFullSchema,
    std::unique_ptr<memprof::DataAccessProfData> DataAccessProfileData,
    std::unique_ptr<memprof::MemProfSummary> MemProfSum);
} // namespace llvm
#endif
