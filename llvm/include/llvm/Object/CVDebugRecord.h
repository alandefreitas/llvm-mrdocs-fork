//===- CVDebugRecord.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_CVDEBUGRECORD_H
#define LLVM_OBJECT_CVDEBUGRECORD_H

#include "llvm/Support/Endian.h"

namespace llvm {
/// Structures for CodeView OMF debug-directory signatures.
namespace OMF {
/// Leading CodeView signature and offset in an OMF debug record.
struct Signature {
  /// Magic values that identify the CodeView / PDB debug-info layout.
  enum ID : uint32_t {
    PDB70 = 0x53445352, ///< RSDS: PDB 7.0 GUID-based debug info.
    PDB20 = 0x3031424e, ///< NB10: PDB 2.0 timestamp-based debug info.
    CV50 = 0x3131424e,  ///< NB11: CodeView 5.0 debug info.
    CV41 = 0x3930424e,  ///< NB09: CodeView 4.1 debug info.
  };

  support::ulittle32_t CVSignature; ///< Magic identifying the debug-info layout.
  support::ulittle32_t Offset;      ///< Offset to the remainder of the record.
};
}

namespace codeview {
/// PDB 7.0 (RSDS) CodeView debug-info payload.
struct PDB70DebugInfo {
  support::ulittle32_t CVSignature; ///< Must be OMF::Signature::PDB70 (RSDS).
  uint8_t Signature[16];            ///< PDB GUID identifying the debug database.
  support::ulittle32_t Age;         ///< PDB age; increments on each rebuild.
  // char PDBFileName[];
};

/// PDB 2.0 (NB10) CodeView debug-info payload.
struct PDB20DebugInfo {
  support::ulittle32_t CVSignature; ///< Must be OMF::Signature::PDB20 (NB10).
  support::ulittle32_t Offset;      ///< Offset field from the NB10 header.
  support::ulittle32_t Signature;   ///< Timestamp signature of the PDB.
  support::ulittle32_t Age;         ///< PDB age; increments on each rebuild.
  // char PDBFileName[];
};

/// Discriminated union of CodeView debug-directory payloads.
union DebugInfo {
  struct OMF::Signature Signature; ///< Shared signature header (layout selector).
  struct PDB20DebugInfo PDB20;     ///< PDB 2.0 (NB10) debug-info layout.
  struct PDB70DebugInfo PDB70;     ///< PDB 7.0 (RSDS) debug-info layout.
};
}
}

#endif
