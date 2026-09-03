//===- InfoStream.h - PDB Info Stream (Stream 1) Access ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_INFOSTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_INFOSTREAM_H

#include "llvm/ADT/StringMap.h"
#include "llvm/DebugInfo/CodeView/GUID.h"
#include "llvm/DebugInfo/PDB/Native/NamedStreamMap.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/Support/BinaryStream.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"

#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {
struct InfoStreamHeader;
/// Provides read access to the PDB Info stream (stream 1).
class InfoStream {
  friend class InfoStreamBuilder;

public:
  /// Construct an Info stream reader over \p Stream.
  ///
  /// \param Stream Owning binary stream for the Info MSF stream.
  LLVM_ABI InfoStream(std::unique_ptr<BinaryStream> Stream);

  /// Reload and reparse the Info stream from the underlying MSF stream.
  ///
  /// \returns An Error on failure, or success if the stream was reloaded.
  LLVM_ABI Error reload();

  /// Return the total size in bytes of the underlying Info stream.
  ///
  /// \returns The total size in bytes of the underlying Info stream.
  LLVM_ABI uint32_t getStreamSize() const;

  /// Return a pointer to the parsed fixed-size Info stream header.
  ///
  /// \returns A pointer to the parsed fixed-size Info stream header.
  const InfoStreamHeader *getHeader() const { return Header; }

  /// Return true if the PDB contains an IPI (ID) stream.
  ///
  /// \returns True if the PDB contains an IPI (ID) stream.
  LLVM_ABI bool containsIdStream() const;
  /// Return the PDB implementation version from the header.
  ///
  /// \returns The PDB implementation version from the header.
  LLVM_ABI PdbRaw_ImplVer getVersion() const;
  /// Return the PDB signature (often a timestamp) from the header.
  ///
  /// \returns The PDB signature from the header.
  LLVM_ABI uint32_t getSignature() const;
  /// Return the PDB age from the header.
  ///
  /// \returns The PDB age from the header.
  LLVM_ABI uint32_t getAge() const;
  /// Return the PDB GUID from the header.
  ///
  /// \returns The PDB GUID from the header.
  LLVM_ABI codeview::GUID getGuid() const;
  /// Return the serialized byte size of the named stream map.
  ///
  /// \returns The serialized byte size of the named stream map.
  LLVM_ABI uint32_t getNamedStreamMapByteSize() const;

  /// Return the decoded feature flags derived from the feature signatures.
  ///
  /// \returns The decoded feature flags derived from the feature signatures.
  LLVM_ABI PdbRaw_Features getFeatures() const;
  /// Return the raw feature signature values from the Info stream.
  ///
  /// \returns The raw feature signature values from the Info stream.
  LLVM_ABI ArrayRef<PdbRaw_FeatureSig> getFeatureSignatures() const;

  /// Return the parsed named stream map.
  ///
  /// \returns A const reference to the parsed named stream map.
  LLVM_ABI const NamedStreamMap &getNamedStreams() const;

  /// Return a reference to the raw named stream map substream bytes.
  ///
  /// \returns A reference to the raw named stream map substream bytes.
  LLVM_ABI BinarySubstreamRef getNamedStreamsBuffer() const;

  /// Look up the MSF stream index for the named stream \p Name.
  ///
  /// \param Name Name of the stream to look up in the named stream map.
  ///
  /// \returns The stream index on success, or an Error if the name is absent.
  LLVM_ABI Expected<uint32_t> getNamedStreamIndex(llvm::StringRef Name) const;
  /// Return a copy of the named stream map as name-to-index entries.
  ///
  /// \returns A StringMap of name-to-index entries from the named stream map.
  LLVM_ABI StringMap<uint32_t> named_streams() const;

private:
  std::unique_ptr<BinaryStream> Stream;

  const InfoStreamHeader *Header;

  BinarySubstreamRef SubNamedStreams;

  std::vector<PdbRaw_FeatureSig> FeatureSignatures;
  PdbRaw_Features Features = PdbFeatureNone;

  uint32_t NamedStreamMapByteSize = 0;

  NamedStreamMap NamedStreams;
};
}
}

#endif
