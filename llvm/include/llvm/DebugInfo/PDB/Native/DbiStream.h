//===- DbiStream.h - PDB Dbi Stream (Stream 3) Access -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_DBISTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_DBISTREAM_H

#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleList.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTable.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"

namespace llvm {
class BinaryStream;
namespace object {
struct FpoData;
struct coff_section;
}
namespace msf {
class MappedBlockStream;
}
namespace pdb {
struct DbiStreamHeader;
struct SecMapEntry;
struct SectionContrib2;
struct SectionContrib;
class PDBFile;
class ISectionContribVisitor;

/// Provides read access to the PDB DBI stream (stream 3).
class DbiStream {
  friend class DbiStreamBuilder;

public:
  /// Construct a DBI stream reader over \p Stream.
  ///
  /// \param Stream Owning binary stream for the DBI MSF stream.
  LLVM_ABI explicit DbiStream(std::unique_ptr<BinaryStream> Stream);
  /// Destroy the DBI stream reader.
  LLVM_ABI ~DbiStream();
  /// Reload and reparse the DBI stream from the underlying MSF stream.
  ///
  /// \param Pdb Owning PDB file used to open related indexed streams.
  ///
  /// \returns An Error on failure, or success if the stream was reloaded.
  LLVM_ABI Error reload(PDBFile *Pdb);

  /// Return the DBI stream format version from the header.
  ///
  /// \returns The DBI stream format version.
  LLVM_ABI PdbRaw_DbiVer getDbiVersion() const;
  /// Return the DBI stream age (should match the Info stream age).
  ///
  /// \returns The DBI stream age.
  LLVM_ABI uint32_t getAge() const;
  /// Return the MSF stream index of the public symbols stream.
  ///
  /// \returns The public symbols stream index.
  LLVM_ABI uint16_t getPublicSymbolStreamIndex() const;
  /// Return the MSF stream index of the global symbols stream.
  ///
  /// \returns The global symbols stream index.
  LLVM_ABI uint16_t getGlobalSymbolStreamIndex() const;

  /// Return the raw DBI header flags bitfield.
  ///
  /// \returns The raw DBI header flags.
  LLVM_ABI uint16_t getFlags() const;
  /// Return true if the PDB was produced by an incremental link.
  ///
  /// \returns True if the PDB was produced by an incremental link.
  LLVM_ABI bool isIncrementallyLinked() const;
  /// Return true if the PDB was linked with /debug:ctypes.
  ///
  /// \returns True if the PDB was linked with /debug:ctypes.
  LLVM_ABI bool hasCTypes() const;
  /// Return true if private symbols were stripped from the PDB.
  ///
  /// \returns True if private symbols were stripped from the PDB.
  LLVM_ABI bool isStripped() const;

  /// Return the packed toolchain build number from the DBI header.
  ///
  /// \returns The packed toolchain build number.
  LLVM_ABI uint16_t getBuildNumber() const;
  /// Return the major toolchain build version encoded in the build number.
  ///
  /// \returns The major toolchain build version.
  LLVM_ABI uint16_t getBuildMajorVersion() const;
  /// Return the minor toolchain build version encoded in the build number.
  ///
  /// \returns The minor toolchain build version.
  LLVM_ABI uint16_t getBuildMinorVersion() const;

  /// Return the mspdbNNN.dll rebuild number from the DBI header.
  ///
  /// \returns The mspdbNNN.dll rebuild number.
  LLVM_ABI uint16_t getPdbDllRbld() const;
  /// Return the mspdbNNN.dll version from the DBI header.
  ///
  /// \returns The mspdbNNN.dll version.
  LLVM_ABI uint32_t getPdbDllVersion() const;

  /// Return the MSF stream index of the symbol records stream.
  ///
  /// \returns The symbol records stream index.
  LLVM_ABI uint32_t getSymRecordStreamIndex() const;

  /// Return the target machine architecture recorded in the DBI header.
  ///
  /// \returns The target machine architecture.
  LLVM_ABI PDB_Machine getMachineType() const;

  /// Return a pointer to the parsed fixed-size DBI stream header.
  ///
  /// \returns A pointer to the parsed DBI stream header.
  const DbiStreamHeader *getHeader() const { return Header; }

  /// Return a reference to the section contribution substream bytes.
  ///
  /// \returns A reference to the section contribution substream bytes.
  LLVM_ABI BinarySubstreamRef getSectionContributionData() const;
  /// Return a reference to the section map substream bytes.
  ///
  /// \returns A reference to the section map substream bytes.
  LLVM_ABI BinarySubstreamRef getSecMapSubstreamData() const;
  /// Return a reference to the module info (Modi) substream bytes.
  ///
  /// \returns A reference to the module info substream bytes.
  LLVM_ABI BinarySubstreamRef getModiSubstreamData() const;
  /// Return a reference to the file info substream bytes.
  ///
  /// \returns A reference to the file info substream bytes.
  LLVM_ABI BinarySubstreamRef getFileInfoSubstreamData() const;
  /// Return a reference to the type server map substream bytes.
  ///
  /// \returns A reference to the type server map substream bytes.
  LLVM_ABI BinarySubstreamRef getTypeServerMapSubstreamData() const;
  /// Return a reference to the edit-and-continue (EC) substream bytes.
  ///
  /// \returns A reference to the EC substream bytes.
  LLVM_ABI BinarySubstreamRef getECSubstreamData() const;

  /// Look up the MSF stream index for an optional debug stream type.
  ///
  /// \param Type Optional debug stream kind to look up in the DBI dbg header.
  ///
  /// \returns The stream index if present, or InvalidStreamIndex if absent.
  LLVM_ABI uint32_t getDebugStreamIndex(DbgHeaderType Type) const;

  /// Return the parsed list of modules and their source files.
  ///
  /// \returns The parsed list of modules and their source files.
  LLVM_ABI const DbiModuleList &modules() const;

  /// Return the COFF section headers from the optional section-header stream.
  ///
  /// \returns The COFF section headers.
  LLVM_ABI FixedStreamArray<object::coff_section> getSectionHeaders() const;

  /// Return true if old-style FPO records are present.
  ///
  /// \returns True if old-style FPO records are present.
  LLVM_ABI bool hasOldFpoRecords() const;
  /// Return the array of old-style FPO frame data records.
  ///
  /// \returns The array of old-style FPO frame data records.
  LLVM_ABI FixedStreamArray<object::FpoData> getOldFpoRecords() const;
  /// Return true if new-style FPO (frame data) records are present.
  ///
  /// \returns True if new-style FPO records are present.
  LLVM_ABI bool hasNewFpoRecords() const;
  /// Return the parsed new-style FPO frame data subsection.
  ///
  /// \returns The parsed new-style FPO frame data subsection.
  LLVM_ABI const codeview::DebugFrameDataSubsectionRef &
  getNewFpoRecords() const;

  /// Return the section map entries from the section map substream.
  ///
  /// \returns The section map entries.
  LLVM_ABI FixedStreamArray<SecMapEntry> getSectionMap() const;
  /// Visit each section contribution with \p Visitor.
  ///
  /// \param Visitor Visitor invoked once per section contribution record.
  LLVM_ABI void
  visitSectionContributions(ISectionContribVisitor &Visitor) const;

  /// Return the version of the section contribution substream format.
  ///
  /// \returns The section contribution substream format version.
  LLVM_ABI PdbRaw_DbiSecContribVer getSectionContributionsVersion() const;

  /// Look up an edit-and-continue name string by name index.
  ///
  /// \param NI Name index into the EC names string table.
  ///
  /// \returns The corresponding string, or an Error if the index is invalid.
  LLVM_ABI Expected<StringRef> getECName(uint32_t NI) const;

private:
  Error initializeSectionContributionData();
  Error initializeSectionHeadersData(PDBFile *Pdb);
  Error initializeSectionMapData();
  Error initializeOldFpoRecords(PDBFile *Pdb);
  Error initializeNewFpoRecords(PDBFile *Pdb);

  Expected<std::unique_ptr<msf::MappedBlockStream>>
  createIndexedStreamForHeaderType(PDBFile *Pdb, DbgHeaderType Type) const;

  std::unique_ptr<BinaryStream> Stream;

  PDBStringTable ECNames;

  BinarySubstreamRef SecContrSubstream;
  BinarySubstreamRef SecMapSubstream;
  BinarySubstreamRef ModiSubstream;
  BinarySubstreamRef FileInfoSubstream;
  BinarySubstreamRef TypeServerMapSubstream;
  BinarySubstreamRef ECSubstream;

  DbiModuleList Modules;

  FixedStreamArray<support::ulittle16_t> DbgStreams;

  PdbRaw_DbiSecContribVer SectionContribVersion =
      PdbRaw_DbiSecContribVer::DbiSecContribVer60;
  FixedStreamArray<SectionContrib> SectionContribs;
  FixedStreamArray<SectionContrib2> SectionContribs2;
  FixedStreamArray<SecMapEntry> SectionMap;

  std::unique_ptr<msf::MappedBlockStream> SectionHeaderStream;
  FixedStreamArray<object::coff_section> SectionHeaders;

  std::unique_ptr<msf::MappedBlockStream> OldFpoStream;
  FixedStreamArray<object::FpoData> OldFpoRecords;
  
  std::unique_ptr<msf::MappedBlockStream> NewFpoStream;
  codeview::DebugFrameDataSubsectionRef NewFpoRecords;

  const DbiStreamHeader *Header;
};
}
}

#endif
