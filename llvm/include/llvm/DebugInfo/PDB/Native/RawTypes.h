//===- RawTypes.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_RAWTYPES_H
#define LLVM_DEBUGINFO_PDB_NATIVE_RAWTYPES_H

#include "llvm/DebugInfo/CodeView/GUID.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/Support/Endian.h"

namespace llvm {
namespace pdb {
/// Section and offset pair identifying a location in an image.
///
/// This struct is defined as "SO" in langapi/include/pdb.h.
struct SectionOffset {
  /// Byte offset within the section.
  support::ulittle32_t Off;
  /// Section index.
  support::ulittle16_t Isect;
  /// Padding to 8-byte alignment.
  char Padding[2];
};

/// Header of the hash tables found in the globals and publics sections.
/// Based on GSIHashHdr in
/// https://github.com/Microsoft/microsoft-pdb/blob/master/PDB/dbi/gsi.h
struct GSIHashHeader {
  /// Magic values identifying a valid GSI hash header.
  enum : unsigned {
    /// Expected signature value written in \c VerSignature.
    HdrSignature = ~0U,
    /// Expected version value written in \c VerHdr.
    HdrVersion = 0xeffe0000 + 19990810,
  };
  /// Hash table signature; should equal \c HdrSignature.
  support::ulittle32_t VerSignature;
  /// Hash table version; should equal \c HdrVersion.
  support::ulittle32_t VerHdr;
  /// Size in bytes of the hash records that follow.
  support::ulittle32_t HrSize;
  /// Number of hash buckets.
  support::ulittle32_t NumBuckets;
};

/// Hash record locating a symbol in the symbol record stream.
///
/// This is HRFile.
struct PSHashRecord {
  /// Offset in the symbol record stream.
  support::ulittle32_t Off;
  /// Reference count for this hash record.
  support::ulittle32_t CRef;
};

/// Section contribution describing one module's contribution to a section.
///
/// This struct is defined as `SC` in include/dbicommon.h
struct SectionContrib {
  /// Section index of the contribution.
  support::ulittle16_t ISect;
  /// Padding to 4-byte alignment.
  char Padding[2];
  /// Offset of the contribution within the section.
  support::little32_t Off;
  /// Size in bytes of the contribution.
  support::little32_t Size;
  /// Section characteristics flags.
  support::ulittle32_t Characteristics;
  /// Module index that owns this contribution.
  support::ulittle16_t Imod;
  /// Padding to 4-byte alignment.
  char Padding2[2];
  /// CRC of the contribution data.
  support::ulittle32_t DataCrc;
  /// CRC of the contribution relocations.
  support::ulittle32_t RelocCrc;
};

/// Extended section contribution that also records the COFF section index.
///
/// This struct is defined as `SC2` in include/dbicommon.h
struct SectionContrib2 {
  // To guarantee SectionContrib2 is standard layout, we cannot use inheritance.
  /// Base section contribution fields.
  SectionContrib Base;
  /// COFF section index of this contribution.
  support::ulittle32_t ISectCoff;
};

/// Header for the OMF segment map table.
///
/// This corresponds to the `OMFSegMap` structure.
struct SecMapHeader {
  /// Number of segment descriptors in the table.
  support::ulittle16_t SecCount;
  /// Number of logical segment descriptors.
  support::ulittle16_t SecCountLog;
};

/// One OMF segment map descriptor entry.
///
/// This corresponds to the `OMFSegMapDesc` structure.  The definition is not
/// present in the reference implementation, but the layout is derived from
/// code that accesses the fields.
struct SecMapEntry {
  /// Descriptor flags. See \c OMFSegDescFlags.
  support::ulittle16_t Flags;
  /// Logical overlay number.
  support::ulittle16_t Ovl;
  /// Group index into the descriptor array.
  support::ulittle16_t Group;
  /// Frame value for the segment (selector or absolute address).
  support::ulittle16_t Frame;
  /// Byte index of the segment or group name in the sstSegName table, or
  /// 0xFFFF.
  support::ulittle16_t SecName;
  /// Byte index of the class name in the sstSegName table, or 0xFFFF.
  support::ulittle16_t ClassName;
  /// Byte offset of the logical segment within the specified physical segment.
  ///
  /// If group is set in flags, offset is the offset of the group.
  support::ulittle32_t Offset;
  /// Byte count of the segment or group.
  support::ulittle32_t SecByteLength;
};

/// Bit masks for flags stored in the DBI stream header.
///
/// Some of the values are stored in bitfields.  Since this needs to be portable
/// across compilers and architectures (big / little endian in particular) we
/// can't use the actual structures below, but must instead do the shifting
/// and masking ourselves.  The struct definitions are provided for reference.
struct DbiFlags {
  //  uint16_t IncrementalLinking : 1; // True if linked incrementally
  //  uint16_t IsStripped : 1;         // True if private symbols were stripped.
  //  uint16_t HasCTypes : 1;          // True if linked with /debug:ctypes.
  //  uint16_t Reserved : 13;
  /// Mask for the incremental linking flag bit.
  static const uint16_t FlagIncrementalMask = 0x0001;
  /// Mask for the private-symbols-stripped flag bit.
  static const uint16_t FlagStrippedMask = 0x0002;
  /// Mask for the /debug:ctypes present flag bit.
  static const uint16_t FlagHasCTypesMask = 0x0004;
};

/// Bit masks and shifts for the DBI stream build number field.
struct DbiBuildNo {
  ///  uint16_t MinorVersion : 8;
  ///  uint16_t MajorVersion : 7;
  ///  uint16_t NewVersionFormat : 1;
  /// Mask for the minor build version bits.
  static const uint16_t BuildMinorMask = 0x00FF;
  /// Bit shift for the minor build version field.
  static const uint16_t BuildMinorShift = 0;

  /// Mask for the major build version bits.
  static const uint16_t BuildMajorMask = 0x7F00;
  /// Bit shift for the major build version field.
  static const uint16_t BuildMajorShift = 8;

  /// Mask for the new-version-format flag bit.
  static const uint16_t NewVersionFormatMask = 0x8000;
};

/// The fixed size header that appears at the beginning of the DBI Stream.
struct DbiStreamHeader {
  /// Signature identifying the DBI stream layout version.
  support::little32_t VersionSignature;
  /// DBI stream format version (see \c PdbRaw_DbiVer).
  support::ulittle32_t VersionHeader;

  /// How "old" is this DBI Stream. Should match the age of the PDB InfoStream.
  support::ulittle32_t Age;

  /// Global symbol stream #
  support::ulittle16_t GlobalSymbolStreamIndex;

  /// See DbiBuildNo structure.
  support::ulittle16_t BuildNumber;

  /// Public symbols stream #
  support::ulittle16_t PublicSymbolStreamIndex;

  /// version of mspdbNNN.dll
  support::ulittle16_t PdbDllVersion;

  /// Symbol records stream #
  support::ulittle16_t SymRecordStreamIndex;

  /// rbld number of mspdbNNN.dll
  support::ulittle16_t PdbDllRbld;

  /// Size of module info stream
  support::little32_t ModiSubstreamSize;

  /// Size of sec. contrib stream
  support::little32_t SecContrSubstreamSize;

  /// Size of sec. map substream
  support::little32_t SectionMapSize;

  /// Size of file info substream
  support::little32_t FileInfoSize;

  /// Size of type server map
  support::little32_t TypeServerSize;

  /// Index of MFC Type Server
  support::ulittle32_t MFCTypeServerIndex;

  /// Size of DbgHeader info
  support::little32_t OptionalDbgHdrSize;

  /// Size of EC stream (what is EC?)
  support::little32_t ECSubstreamSize;

  /// See DbiFlags enum.
  support::ulittle16_t Flags;

  /// See PDB_MachineType enum.
  support::ulittle16_t MachineType;

  /// Pad to 64 bytes
  support::ulittle32_t Reserved;
};
static_assert(sizeof(DbiStreamHeader) == 64, "Invalid DbiStreamHeader size!");

/// The header preceding the File Info Substream of the DBI stream.
struct FileInfoSubstreamHeader {
  /// Total # of modules, should match number of records in the ModuleInfo
  /// substream.
  support::ulittle16_t NumModules;

  /// Total number of source files across all modules.
  ///
  /// This value is not accurate because PDB actually supports more than 64k
  /// source files, so we ignore it and compute the value from other stream
  /// fields.
  support::ulittle16_t NumSourceFiles;

  /// Following this header the File Info Substream is laid out as follows:
  ///   ulittle16_t ModIndices[NumModules];
  ///   ulittle16_t ModFileCounts[NumModules];
  ///   ulittle32_t FileNameOffsets[NumSourceFiles];
  ///   char Names[][NumSourceFiles];
  /// with the caveat that `NumSourceFiles` cannot be trusted, so
  /// it is computed by summing the `ModFileCounts` array.
};

/// Bit masks and shifts for per-module info flags.
struct ModInfoFlags {
  //  uint16_t fWritten : 1;   // True if DbiModuleDescriptor is dirty
  //  uint16_t fECEnabled : 1; // Is EC symbolic info present?  (What is EC?)
  //  uint16_t unused : 6;     // Reserved
  //  uint16_t iTSM : 8;       // Type Server Index for this module
  /// Mask for the EC symbolic info present flag bit.
  static const uint16_t HasECFlagMask = 0x2;

  /// Mask for the type server index field.
  static const uint16_t TypeServerIndexMask = 0xFF00;
  /// Bit shift for the type server index field.
  static const uint16_t TypeServerIndexShift = 8;
};

/// The header preceding each entry in the Module Info substream of the DBI
/// stream.  Corresponds to the type MODI in the reference implementation.
struct ModuleInfoHeader {
  /// Currently opened module handle; unused when reading from a file.
  ///
  /// This field is a pointer in the reference implementation, but that won't
  /// work on 64-bit systems, and anyway it doesn't make sense to read a
  /// pointer from a file. For now it is unused, so just ignore it.
  support::ulittle32_t Mod;

  /// First section contribution of this module.
  SectionContrib SC;

  /// See ModInfoFlags definition.
  support::ulittle16_t Flags;

  /// Stream Number of module debug info
  support::ulittle16_t ModDiStream;

  /// Size of local symbol debug info in above stream
  support::ulittle32_t SymBytes;

  /// Size of C11 line number info in above stream
  support::ulittle32_t C11Bytes;

  /// Size of C13 line number info in above stream
  support::ulittle32_t C13Bytes;

  /// Number of files contributing to this module
  support::ulittle16_t NumFiles;

  /// Padding so the next field is 4-byte aligned.
  char Padding1[2];

  /// Unused DBI name-buffer offset slot reserved for a runtime pointer.
  ///
  /// Array of [0..NumFiles) DBI name buffer offsets.  In the reference
  /// implementation this field is a pointer.  But since you can't portably
  /// serialize a pointer, on 64-bit platforms they copy all the values except
  /// this one into the 32-bit version of the struct and use that for
  /// serialization.  Regardless, this field is unused, it is only there to
  /// store a pointer that can be accessed at runtime.
  support::ulittle32_t FileNameOffs;

  /// Name Index for src file name
  support::ulittle32_t SrcFileNameNI;

  /// Name Index for path to compiler PDB
  support::ulittle32_t PdbFilePathNI;

  /// Following this header are two zero terminated strings.
  /// char ModuleName[];
  /// char ObjFileName[];
};

/// Header of the publics stream (PSGSIHDR).
///
/// This is PSGSIHDR struct defined in
/// https://github.com/Microsoft/microsoft-pdb/blob/master/PDB/dbi/gsi.h
struct PublicsStreamHeader {
  /// Size in bytes of the symbol hash substream.
  support::ulittle32_t SymHash;
  /// Size in bytes of the address map substream.
  support::ulittle32_t AddrMap;
  /// Number of thunk entries.
  support::ulittle32_t NumThunks;
  /// Size in bytes of each thunk.
  support::ulittle32_t SizeOfThunk;
  /// Section index of the thunk table.
  support::ulittle16_t ISectThunkTable;
  /// Padding to 4-byte alignment.
  char Padding[2];
  /// Offset of the thunk table within its section.
  support::ulittle32_t OffThunkTable;
  /// Number of sections described by the publics stream.
  support::ulittle32_t NumSections;
};

/// Header preceding the global TPI (or IPI) stream.
///
/// This corresponds to `HDR` in PDB/dbi/tpi.h.
struct TpiStreamHeader {
  /// Offset and length of an embedded buffer within the TPI hash stream.
  struct EmbeddedBuf {
    /// Offset of the buffer within the hash stream.
    support::little32_t Off;
    /// Length in bytes of the buffer.
    support::ulittle32_t Length;
  };

  /// TPI stream format version (see \c PdbRaw_TpiVer).
  support::ulittle32_t Version;
  /// Size in bytes of this header.
  support::ulittle32_t HeaderSize;
  /// First type index covered by this stream.
  support::ulittle32_t TypeIndexBegin;
  /// One past the last type index covered by this stream.
  support::ulittle32_t TypeIndexEnd;
  /// Total size in bytes of the type records that follow.
  support::ulittle32_t TypeRecordBytes;

  // The following members correspond to `TpiHash` in PDB/dbi/tpi.h.
  /// Stream index of the primary TPI hash stream.
  support::ulittle16_t HashStreamIndex;
  /// Stream index of the auxiliary TPI hash stream.
  support::ulittle16_t HashAuxStreamIndex;
  /// Size in bytes of each hash key.
  support::ulittle32_t HashKeySize;
  /// Number of hash buckets.
  support::ulittle32_t NumHashBuckets;

  /// Buffer of hash values within the hash stream.
  EmbeddedBuf HashValueBuffer;
  /// Buffer of type-index offsets within the hash stream.
  EmbeddedBuf IndexOffsetBuffer;
  /// Buffer of hash adjustments within the hash stream.
  EmbeddedBuf HashAdjBuffer;
};

/// Minimum supported number of TPI hash buckets.
const uint32_t MinTpiHashBuckets = 0x1000;
/// Maximum supported number of TPI hash buckets.
const uint32_t MaxTpiHashBuckets = 0x40000;

/// The header preceding the global PDB Stream (Stream 1)
struct InfoStreamHeader {
  /// PDB implementation version (see \c PdbRaw_ImplVer).
  support::ulittle32_t Version;
  /// Signature used to identify the PDB (often a timestamp).
  support::ulittle32_t Signature;
  /// Age of the PDB; incremented on each write.
  support::ulittle32_t Age;
  /// Unique GUID identifying this PDB.
  codeview::GUID Guid;
};

/// The header preceding the /names stream.
struct PDBStringTableHeader {
  /// String table signature; should equal \c PDBStringTableSignature.
  support::ulittle32_t Signature;
  /// Hash algorithm version (1 or 2).
  support::ulittle32_t HashVersion;
  /// Number of bytes of names buffer.
  support::ulittle32_t ByteSize;
};

/// Expected signature value for \c PDBStringTableHeader::Signature.
const uint32_t PDBStringTableSignature = 0xEFFEEFFE;

/// The header preceding the /src/headerblock stream.
struct SrcHeaderBlockHeader {
  /// Stream format version (\c PdbRaw_SrcHeaderBlockVer).
  support::ulittle32_t Version;
  /// Size of entire stream.
  support::ulittle32_t Size;
  /// Time stamp (Windows FILETIME format).
  uint64_t FileTime;
  /// Age of the source header block.
  support::ulittle32_t Age;
  /// Pad to 64 bytes.
  uint8_t Padding[44];
};
static_assert(sizeof(SrcHeaderBlockHeader) == 64, "Incorrect struct size!");

/// A single file record entry within the /src/headerblock stream.
struct SrcHeaderBlockEntry {
  /// Record length in bytes.
  support::ulittle32_t Size;
  /// Record format version (\c PdbRaw_SrcHeaderBlockVer).
  support::ulittle32_t Version;
  /// CRC of the original file contents.
  support::ulittle32_t CRC;
  /// Size of original source file.
  support::ulittle32_t FileSize;
  /// String table index of file name.
  support::ulittle32_t FileNI;
  /// String table index of object name.
  support::ulittle32_t ObjNI;
  /// String table index of virtual file name.
  support::ulittle32_t VFileNI;
  /// Compression kind (\c PDB_SourceCompression).
  uint8_t Compression;
  /// Non-zero if this is a virtual (injected) file.
  uint8_t IsVirtual;
  /// Pad to 4 bytes.
  short Padding;
  /// Reserved bytes; must be zero.
  char Reserved[8];
};
static_assert(sizeof(SrcHeaderBlockEntry) == 40, "Incorrect struct size!");

} // namespace pdb
} // namespace llvm

#endif
