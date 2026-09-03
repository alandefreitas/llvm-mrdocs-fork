//===- DbiStreamBuilder.h - PDB Dbi Stream Creation -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_DBISTREAMBUILDER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_DBISTREAMBUILDER_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTableBuilder.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamRef.h"

namespace llvm {

class BinaryStreamWriter;
namespace codeview {
struct FrameData;
}
namespace msf {
class MSFBuilder;
struct MSFLayout;
}
namespace pdb {
class DbiModuleDescriptorBuilder;

/// Builds the PDB Debug Information (DBI) stream and its related substreams.
class DbiStreamBuilder {
public:
  /// Construct a DBI stream builder that allocates streams in \p Msf.
  ///
  /// \param Msf The MSF builder that owns the PDB streams.
  LLVM_ABI DbiStreamBuilder(msf::MSFBuilder &Msf);
  /// Destroy the DBI stream builder.
  LLVM_ABI ~DbiStreamBuilder();

  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is not supported.
  DbiStreamBuilder(const DbiStreamBuilder &Other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param Other Unused; copy assignment is not supported.
  DbiStreamBuilder &operator=(const DbiStreamBuilder &Other) = delete;

  /// Set the DBI stream version header field.
  ///
  /// \param V The PDB DBI version to store in the stream header.
  LLVM_ABI void setVersionHeader(PdbRaw_DbiVer V);
  /// Set the DBI stream age field.
  ///
  /// \param A The age value written into the DBI header.
  LLVM_ABI void setAge(uint32_t A);
  /// Set the DBI build number from a packed 16-bit value.
  ///
  /// \param B The packed build number stored directly in the DBI header.
  LLVM_ABI void setBuildNumber(uint16_t B);
  /// Set the DBI build number from major and minor toolchain components.
  ///
  /// \param Major The major build number component.
  /// \param Minor The minor build number component.
  LLVM_ABI void setBuildNumber(uint8_t Major, uint8_t Minor);
  /// Set the PDB DLL version field in the DBI header.
  ///
  /// \param V The mspdb DLL version value.
  LLVM_ABI void setPdbDllVersion(uint16_t V);
  /// Set the PDB DLL rebuild field in the DBI header.
  ///
  /// \param R The mspdb DLL rebuild number.
  LLVM_ABI void setPdbDllRbld(uint16_t R);
  /// Set the DBI header flags field.
  ///
  /// \param F The flags value written into the DBI header.
  LLVM_ABI void setFlags(uint16_t F);
  /// Set the target machine type from a PDB machine enumeration.
  ///
  /// \param M The PDB machine type stored in the DBI header.
  LLVM_ABI void setMachineType(PDB_Machine M);
  /// Set the target machine type from a COFF machine enumeration.
  ///
  /// \param M The COFF machine type, cast to the matching PDB machine value.
  LLVM_ABI void setMachineType(COFF::MachineTypes M);

  /// Add given bytes as a new debug data stream of the specified type.
  ///
  /// \param Type The debug header type identifying which dbg stream to create.
  /// \param Data The raw bytes that form the contents of the dbg stream.
  /// \return Success, or an error if the dbg stream cannot be added.
  LLVM_ABI Error addDbgStream(pdb::DbgHeaderType Type, ArrayRef<uint8_t> Data);

  /// Insert a name into the edit-and-continue (EC) name table.
  ///
  /// \param Name The EC name string to insert.
  /// \return The string table index of \p Name.
  LLVM_ABI uint32_t addECName(StringRef Name);

  /// Calculate the total serialized size of the DBI stream in bytes.
  ///
  /// \return The number of bytes required to serialize the DBI stream.
  LLVM_ABI uint32_t calculateSerializedLength() const;

  /// Set the MSF stream index of the globals stream.
  ///
  /// \param Index The stream index stored in the DBI header.
  LLVM_ABI void setGlobalsStreamIndex(uint32_t Index);
  /// Set the MSF stream index of the publics stream.
  ///
  /// \param Index The stream index stored in the DBI header.
  LLVM_ABI void setPublicsStreamIndex(uint32_t Index);
  /// Set the MSF stream index of the symbol record stream.
  ///
  /// \param Index The stream index stored in the DBI header.
  LLVM_ABI void setSymbolRecordStreamIndex(uint32_t Index);
  /// Add a CodeView frame-data record to the new FPO dbg stream.
  ///
  /// \param FD The frame-data record to append.
  LLVM_ABI void addNewFpoData(const codeview::FrameData &FD);
  /// Add a legacy FPO record to the old FPO dbg stream.
  ///
  /// \param Fpo The legacy FPO data record to append.
  LLVM_ABI void addOldFpoData(const object::FpoData &Fpo);

  /// Add a module descriptor for the given module name.
  ///
  /// \param ModuleName The object or module name used for the descriptor.
  /// \return A reference to the new module descriptor builder, or an error.
  LLVM_ABI Expected<DbiModuleDescriptorBuilder &>
  addModuleInfo(StringRef ModuleName);
  /// Associate a source file name with an existing module descriptor.
  ///
  /// \param Module The module descriptor builder that owns the source file.
  /// \param File The source file path to record for \p Module.
  /// \return Success, or an error if the association cannot be recorded.
  LLVM_ABI Error addModuleSourceFile(DbiModuleDescriptorBuilder &Module,
                                     StringRef File);
  /// Look up the source-file name table index for \p FileName.
  ///
  /// \param FileName The source file path previously registered with the builder.
  /// \return The name table index, or an error if the file was not found.
  LLVM_ABI Expected<uint32_t> getSourceFileNameIndex(StringRef FileName);

  /// Finalize MSF stream layout for the DBI stream and its related streams.
  ///
  /// \return Success, or an error if stream allocation fails.
  LLVM_ABI Error finalizeMsfLayout();

  /// Commit the DBI stream and related substreams into \p MsfBuffer.
  ///
  /// \param Layout The finalized MSF layout describing stream positions.
  /// \param MsfBuffer Writable view of the MSF file into which data is written.
  /// \return Success, or an error if writing any stream fails.
  LLVM_ABI Error commit(const msf::MSFLayout &Layout,
                        WritableBinaryStreamRef MsfBuffer);

  /// Append a section contribution entry to the DBI section contribs list.
  ///
  /// \param SC The section contribution to record.
  void addSectionContrib(const SectionContrib &SC) {
    SectionContribs.emplace_back(SC);
  }

  /// Populate the section map from COFF section headers.
  ///
  /// \param SecHdrs The COFF section headers used to build the section map.
  LLVM_ABI void createSectionMap(ArrayRef<llvm::object::coff_section> SecHdrs);

private:
  struct DebugStream {
    std::function<Error(BinaryStreamWriter &)> WriteFn;
    uint32_t Size = 0;
    uint16_t StreamNumber = kInvalidStreamIndex;
  };

  Error finalize();
  uint32_t calculateModiSubstreamSize() const;
  uint32_t calculateNamesOffset() const;
  uint32_t calculateSectionContribsStreamSize() const;
  uint32_t calculateSectionMapStreamSize() const;
  uint32_t calculateFileInfoSubstreamSize() const;
  uint32_t calculateNamesBufferSize() const;
  uint32_t calculateDbgStreamsSize() const;

  Error generateFileInfoSubstream();

  msf::MSFBuilder &Msf;
  BumpPtrAllocator &Allocator;

  std::optional<PdbRaw_DbiVer> VerHeader;
  uint32_t Age;
  uint16_t BuildNumber;
  uint16_t PdbDllVersion;
  uint16_t PdbDllRbld;
  uint16_t Flags;
  PDB_Machine MachineType;
  uint32_t GlobalsStreamIndex = kInvalidStreamIndex;
  uint32_t PublicsStreamIndex = kInvalidStreamIndex;
  uint32_t SymRecordStreamIndex = kInvalidStreamIndex;

  const DbiStreamHeader *Header;

  std::vector<std::unique_ptr<DbiModuleDescriptorBuilder>> ModiList;

  std::optional<codeview::DebugFrameDataSubsection> NewFpoData;
  std::vector<object::FpoData> OldFpoData;

  StringMap<uint32_t> SourceFileNames;

  PDBStringTableBuilder ECNamesBuilder;
  WritableBinaryStreamRef NamesBuffer;
  MutableBinaryByteStream FileInfoBuffer;
  std::vector<SectionContrib> SectionContribs;
  std::vector<SecMapEntry> SectionMap;
  std::array<std::optional<DebugStream>, (int)DbgHeaderType::Max> DbgStreams;
};
} // namespace pdb
}

#endif
