//===- InfoStreamBuilder.h - PDB Info Stream Creation -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_INFOSTREAMBUILDER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_INFOSTREAMBUILDER_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

#include "llvm/DebugInfo/CodeView/GUID.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"

namespace llvm {
class WritableBinaryStreamRef;

namespace msf {
class MSFBuilder;
struct MSFLayout;
}
namespace pdb {
class NamedStreamMap;

/// Builds the PDB Info stream (stream 1), including version, age, GUID, and
/// feature signatures.
class InfoStreamBuilder {
public:
  /// Construct an info stream builder that allocates in \p Msf.
  ///
  /// \param Msf The MSF builder that owns the PDB streams.
  /// \param NamedStreams The named stream map serialized into the info stream.
  LLVM_ABI InfoStreamBuilder(msf::MSFBuilder &Msf,
                             NamedStreamMap &NamedStreams);
  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is not supported.
  InfoStreamBuilder(const InfoStreamBuilder &Other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param Other Unused; copy assignment is not supported.
  InfoStreamBuilder &operator=(const InfoStreamBuilder &Other) = delete;

  /// Set the PDB implementation version written into the info stream header.
  ///
  /// \param V The PDB raw implementation version to store.
  LLVM_ABI void setVersion(PdbRaw_ImplVer V);
  /// Append a feature signature to the info stream feature list.
  ///
  /// \param Sig The feature signature to add.
  LLVM_ABI void addFeature(PdbRaw_FeatureSig Sig);

  /// Set whether to derive the GUID and signature from a hash of the PDB.
  ///
  /// If this is true, the PDB contents are hashed and this hash is used as
  /// PDB GUID and as Signature. The age is always 1.
  ///
  /// \param B True to hash PDB contents into the GUID and signature.
  LLVM_ABI void setHashPDBContentsToGUID(bool B);

  /// Set the optional PDB signature field.
  ///
  /// Has an effect only if hashPDBContentsToGUID() is false.
  ///
  /// \param S The signature value written into the info stream header.
  LLVM_ABI void setSignature(uint32_t S);
  /// Set the PDB age field.
  ///
  /// Has an effect only if hashPDBContentsToGUID() is false.
  ///
  /// \param A The age value written into the info stream header.
  LLVM_ABI void setAge(uint32_t A);
  /// Set the PDB GUID field.
  ///
  /// Has an effect only if hashPDBContentsToGUID() is false.
  ///
  /// \param G The GUID written into the info stream header.
  LLVM_ABI void setGuid(codeview::GUID G);

  /// Return whether the GUID and signature are derived from hashing the PDB.
  ///
  /// \return True if the GUID and signature come from a hash of the PDB.
  bool hashPDBContentsToGUID() const { return HashPDBContentsToGUID; }
  /// Return the PDB age that will be written into the info stream.
  ///
  /// \return The age value stored for the info stream header.
  uint32_t getAge() const { return Age; }
  /// Return the PDB GUID that will be written into the info stream.
  ///
  /// \return The GUID stored for the info stream header.
  codeview::GUID getGuid() const { return Guid; }
  /// Return the optional PDB signature, if one was set.
  ///
  /// \return The signature value, or an empty optional if none was set.
  std::optional<uint32_t> getSignature() const { return Signature; }

  /// Finalize the info stream and return its serialized length in bytes.
  ///
  /// \return The serialized size of the info stream in bytes.
  LLVM_ABI uint32_t finalize();

  /// Finalize MSF stream layout for the info stream.
  ///
  /// \return Success, or an error if stream allocation fails.
  LLVM_ABI Error finalizeMsfLayout();

  /// Commit the info stream into \p Buffer.
  ///
  /// \param Layout The finalized MSF layout describing stream positions.
  /// \param Buffer Writable view of the MSF file into which data is written.
  /// \return Success, or an error if writing the info stream fails.
  LLVM_ABI Error commit(const msf::MSFLayout &Layout,
                        WritableBinaryStreamRef Buffer) const;

private:
  msf::MSFBuilder &Msf;

  std::vector<PdbRaw_FeatureSig> Features;
  PdbRaw_ImplVer Ver;
  uint32_t Age;
  std::optional<uint32_t> Signature;
  codeview::GUID Guid;

  bool HashPDBContentsToGUID = false;

  NamedStreamMap &NamedStreams;
};
} // namespace pdb
}

#endif
