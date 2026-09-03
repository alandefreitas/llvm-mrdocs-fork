//===- RawConstants.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_RAWCONSTANTS_H
#define LLVM_DEBUGINFO_PDB_NATIVE_RAWCONSTANTS_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include <cstdint>

namespace llvm {
namespace pdb {

/// Sentinel stream index indicating that a stream is absent or unset.
const uint16_t kInvalidStreamIndex = 0xFFFF;

/// PDB file format implementation version numbers written in the info stream.
enum PdbRaw_ImplVer : uint32_t {
  PdbImplVC2 = 19941610,     ///< Visual C++ 2.0 PDB format.
  PdbImplVC4 = 19950623,     ///< Visual C++ 4.0 PDB format.
  PdbImplVC41 = 19950814,    ///< Visual C++ 4.1 PDB format.
  PdbImplVC50 = 19960307,    ///< Visual C++ 5.0 PDB format.
  PdbImplVC98 = 19970604,    ///< Visual C++ 6.0 / VC98 PDB format.
  PdbImplVC70Dep = 19990604, ///< Deprecated Visual C++ 7.0 PDB format.
  PdbImplVC70 = 20000404,    ///< Visual C++ 7.0 PDB format.
  PdbImplVC80 = 20030901,    ///< Visual C++ 8.0 PDB format.
  PdbImplVC110 = 20091201,   ///< Visual C++ 11.0 PDB format.
  PdbImplVC140 = 20140508,   ///< Visual C++ 14.0 PDB format.
};

/// Version of the injected-source header block in the PDB.
enum class PdbRaw_SrcHeaderBlockVer : uint32_t {
  SrcVerOne = 19980827 ///< Sole known source-header block version.
};

/// Feature signatures appended to the PDB info stream.
enum class PdbRaw_FeatureSig : uint32_t {
  VC110 = PdbImplVC110,          ///< Visual C++ 11.0 feature signature.
  VC140 = PdbImplVC140,          ///< Visual C++ 14.0 feature signature.
  NoTypeMerge = 0x4D544F4E,      ///< Types were not merged ('NOTM').
  MinimalDebugInfo = 0x494E494D, ///< Minimal debug info present ('MINI').
};

/// Feature flags derived from the PDB info stream feature signatures.
enum PdbRaw_Features : uint32_t {
  PdbFeatureNone = 0x0,              ///< No features are set.
  PdbFeatureContainsIdStream = 0x1,  ///< PDB contains an IPI (ID) stream.
  PdbFeatureMinimalDebugInfo = 0x2,  ///< PDB was produced with minimal debug info.
  PdbFeatureNoTypeMerging = 0x4,     ///< Type records were not merged.
  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ PdbFeatureNoTypeMerging)
};

/// DBI stream format version numbers.
enum PdbRaw_DbiVer : uint32_t {
  PdbDbiVC41 = 930803,    ///< Visual C++ 4.1 DBI format.
  PdbDbiV50 = 19960307,   ///< Visual C++ 5.0 DBI format.
  PdbDbiV60 = 19970606,   ///< Visual C++ 6.0 DBI format.
  PdbDbiV70 = 19990903,   ///< Visual C++ 7.0 DBI format.
  PdbDbiV110 = 20091201   ///< Visual C++ 11.0 DBI format.
};

/// TPI stream format version numbers.
enum PdbRaw_TpiVer : uint32_t {
  PdbTpiV40 = 19950410, ///< Visual C++ 4.0 TPI format.
  PdbTpiV41 = 19951122, ///< Visual C++ 4.1 TPI format.
  PdbTpiV50 = 19961031, ///< Visual C++ 5.0 TPI format.
  PdbTpiV70 = 19990903, ///< Visual C++ 7.0 TPI format.
  PdbTpiV80 = 20040203, ///< Visual C++ 8.0 TPI format.
};

/// Section-contribution substream version numbers in the DBI stream.
enum PdbRaw_DbiSecContribVer : uint32_t {
  DbiSecContribVer60 = 0xeffe0000 + 19970605, ///< Original section-contribution layout.
  DbiSecContribV2 = 0xeffe0000 + 20140516     ///< Extended section-contribution layout (SC2).
};

/// Fixed stream indices reserved for the well-known PDB streams.
enum SpecialStream : uint32_t {
  /// Stream 0 contains the copy of previous version of the MSF directory.
  /// We are not currently using it, but technically if we find the main
  /// MSF is corrupted, we could fallback to it.
  OldMSFDirectory = 0,

  StreamPDB = 1, ///< PDB info stream.
  StreamTPI = 2, ///< Type information (TPI) stream.
  StreamDBI = 3, ///< Debug information (DBI) stream.
  StreamIPI = 4, ///< ID information (IPI) stream.

  kSpecialStreamCount = 5, ///< Number of reserved special-stream indices.
  /// Fixed index of DXContainer stream, but it's not one of the special
  /// streams and is produced only by DirectX tools.
  StreamDXContainer = 5
};

/// Kinds of optional debug streams indexed from the DBI stream header.
enum class DbgHeaderType : uint16_t {
  FPO,            ///< Frame pointer omission (FPO) data.
  Exception,      ///< Exception data.
  Fixup,          ///< Fixup data.
  OmapToSrc,      ///< OMAP mapping from image to source.
  OmapFromSrc,    ///< OMAP mapping from source to image.
  SectionHdr,     ///< Section headers.
  TokenRidMap,    ///< Token-to-RID map.
  Xdata,          ///< Exception unwind data (.xdata).
  Pdata,          ///< Exception function table (.pdata).
  NewFPO,         ///< Newer FPO / frame data format.
  SectionHdrOrig, ///< Original section headers (before OMAP).
  Max             ///< Sentinel one past the last valid debug header type.
};

/// Flags describing an OMF segment map descriptor entry.
enum class OMFSegDescFlags : uint16_t {
  None = 0,                   ///< No flags are set.
  Read = 1 << 0,              ///< Segment is readable.
  Write = 1 << 1,             ///< Segment is writable.
  Execute = 1 << 2,           ///< Segment is executable.
  AddressIs32Bit = 1 << 3,    ///< Descriptor describes a 32-bit linear address.
  IsSelector = 1 << 8,        ///< Frame represents a selector.
  IsAbsoluteAddress = 1 << 9, ///< Frame represents an absolute address.
  IsGroup = 1 << 10,          ///< If set, descriptor represents a group.
  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ IsGroup)
};

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_RAWCONSTANTS_H
