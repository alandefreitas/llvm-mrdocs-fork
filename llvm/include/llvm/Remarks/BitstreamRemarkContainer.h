//===-- BitstreamRemarkContainer.h - Container for remarks --------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides declarations for things used in the various types of
// remark containers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_BITSTREAMREMARKCONTAINER_H
#define LLVM_REMARKS_BITSTREAMREMARKCONTAINER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Bitstream/BitCodes.h"
#include <cstdint>

namespace llvm {
namespace remarks {

/// The current version of the remark container.
/// Note: this is different from the version of the remark entry.
constexpr uint64_t CurrentContainerVersion = 1;
/// The magic number used for identifying remark blocks.
constexpr StringLiteral ContainerMagic("RMRK");

/// Type of the remark container.
enum class BitstreamRemarkContainerType {
  /// Emit a link to an external remarks file
  /// (usually as a section of the object file, to enable discovery of all
  /// remarks files from the final linked object file)
  /// RemarksFileExternal:
  ///   | Meta:
  ///   | | Container info
  ///   | | External file
  RemarksFileExternal,
  /// Emit metadata and remarks into a file
  /// RemarksFile:
  ///   | Meta:
  ///   | | Container info
  ///   | | Remark version
  ///   | Remarks:
  ///   | | Remark0
  ///   | | Remark1
  ///   | | Remark2
  ///   | | ...
  ///   | Late Meta:
  ///   | | String table
  RemarksFile,
  First = RemarksFileExternal,
  Last = RemarksFile
};

/// The possible blocks that will be encountered in a bitstream remark
/// container.
enum BlockIDs {
  /// The metadata block is mandatory. It should always come after the
  /// BLOCKINFO_BLOCK, and contains metadata that should be used when parsing
  /// REMARK_BLOCKs.
  /// There should always be only one META_BLOCK.
  META_BLOCK_ID = bitc::FIRST_APPLICATION_BLOCKID,
  /// One remark entry is represented using a REMARK_BLOCK. There can be
  /// multiple REMARK_BLOCKs in the same file.
  REMARK_BLOCK_ID
};

/// The human-readable name of the META_BLOCK in the bitstream block info.
constexpr StringLiteral MetaBlockName("Meta");
/// The human-readable name of the REMARK_BLOCK in the bitstream block info.
constexpr StringLiteral RemarkBlockName("Remark");

/// The possible records that can be encountered in the previously described
/// blocks.
enum RecordIDs {
  // Meta block records.
  /// Record describing the container type and version.
  RECORD_META_CONTAINER_INFO = 1,
  /// Record describing the remark entry version.
  RECORD_META_REMARK_VERSION,
  /// Record holding the string table used by remarks.
  RECORD_META_STRTAB,
  /// Record linking to an external remarks file.
  RECORD_META_EXTERNAL_FILE,
  // Remark block records.
  /// Record holding the main remark header fields.
  RECORD_REMARK_HEADER,
  /// Record holding the debug location of a remark.
  RECORD_REMARK_DEBUG_LOC,
  /// Record holding the hotness of a remark.
  RECORD_REMARK_HOTNESS,
  /// Record holding a remark argument that includes a debug location.
  RECORD_REMARK_ARG_WITH_DEBUGLOC,
  /// Record holding a remark argument without a debug location.
  RECORD_REMARK_ARG_WITHOUT_DEBUGLOC,
  // Helpers.
  /// First valid record ID in this enumeration.
  RECORD_FIRST = RECORD_META_CONTAINER_INFO,
  /// Last valid record ID in this enumeration.
  RECORD_LAST = RECORD_REMARK_ARG_WITHOUT_DEBUGLOC
};

/// The human-readable name of the RECORD_META_CONTAINER_INFO record.
constexpr StringLiteral MetaContainerInfoName("Container info");
/// The human-readable name of the RECORD_META_REMARK_VERSION record.
constexpr StringLiteral MetaRemarkVersionName("Remark version");
/// The human-readable name of the RECORD_META_STRTAB record.
constexpr StringLiteral MetaStrTabName("String table");
/// The human-readable name of the RECORD_META_EXTERNAL_FILE record.
constexpr StringLiteral MetaExternalFileName("External File");
/// The human-readable name of the RECORD_REMARK_HEADER record.
constexpr StringLiteral RemarkHeaderName("Remark header");
/// The human-readable name of the RECORD_REMARK_DEBUG_LOC record.
constexpr StringLiteral RemarkDebugLocName("Remark debug location");
/// The human-readable name of the RECORD_REMARK_HOTNESS record.
constexpr StringLiteral RemarkHotnessName("Remark hotness");
/// The human-readable name of the RECORD_REMARK_ARG_WITH_DEBUGLOC record.
constexpr StringLiteral
    RemarkArgWithDebugLocName("Argument with debug location");
/// The human-readable name of the RECORD_REMARK_ARG_WITHOUT_DEBUGLOC record.
constexpr StringLiteral RemarkArgWithoutDebugLocName("Argument");

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_BITSTREAMREMARKCONTAINER_H
