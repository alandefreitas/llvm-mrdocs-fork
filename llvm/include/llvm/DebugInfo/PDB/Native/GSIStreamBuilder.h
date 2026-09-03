//===- GSIStreamBuilder.h - PDB Publics/Globals Stream Creation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_GSISTREAMBUILDER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_GSISTREAMBUILDER_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
class ConstantSym;
class DataSym;
class ProcRefSym;
} // namespace codeview
template <typename T> struct BinaryItemTraits;

/// BinaryItemTraits specialization for CodeView symbol records.
template <> struct BinaryItemTraits<codeview::CVSymbol> {
  /// Return the serialized length in bytes of \p Item.
  ///
  /// \param Item The symbol record whose length is requested.
  /// \return The size in bytes of \p Item's record data.
  static size_t length(const codeview::CVSymbol &Item) {
    return Item.RecordData.size();
  }
  /// Return the raw bytes that make up \p Item.
  ///
  /// \param Item The symbol record whose bytes are requested.
  /// \return The record data bytes of \p Item.
  static ArrayRef<uint8_t> bytes(const codeview::CVSymbol &Item) {
    return Item.RecordData;
  }
};

namespace msf {
class MSFBuilder;
struct MSFLayout;
} // namespace msf
namespace pdb {
/// Builder for the hash tables used by the PDB publics and globals streams.
struct GSIHashStreamBuilder;
struct BulkPublic;
/// DenseMapInfo used to deduplicate global typedef and constant symbol records.
struct SymbolDenseMapInfo;

/// Builds the PDB globals, publics, and symbol record streams.
class GSIStreamBuilder {

public:
  /// Construct a GSI stream builder that allocates streams in \p Msf.
  ///
  /// \param Msf The MSF builder that owns the PDB streams.
  LLVM_ABI explicit GSIStreamBuilder(msf::MSFBuilder &Msf);
  /// Destroy the GSI stream builder.
  LLVM_ABI ~GSIStreamBuilder();

  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is not supported.
  GSIStreamBuilder(const GSIStreamBuilder &Other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param Other Unused; copy assignment is not supported.
  GSIStreamBuilder &operator=(const GSIStreamBuilder &Other) = delete;

  /// Finalize MSF stream layout for the publics, globals, and record streams.
  ///
  /// \return Success, or an error if stream allocation fails.
  LLVM_ABI Error finalizeMsfLayout();

  /// Commit the publics, globals, and symbol record streams into \p Buffer.
  ///
  /// \param Layout The finalized MSF layout describing stream positions.
  /// \param Buffer Writable view of the MSF file into which streams are written.
  /// \return Success, or an error if writing any stream fails.
  LLVM_ABI Error commit(const msf::MSFLayout &Layout,
                        WritableBinaryStreamRef Buffer);

  /// Return the MSF stream index of the publics stream.
  ///
  /// \return The publics stream index within the MSF container.
  uint32_t getPublicsStreamIndex() const { return PublicsStreamIndex; }
  /// Return the MSF stream index of the globals stream.
  ///
  /// \return The globals stream index within the MSF container.
  uint32_t getGlobalsStreamIndex() const { return GlobalsStreamIndex; }
  /// Return the MSF stream index of the symbol record stream.
  ///
  /// \return The symbol record stream index within the MSF container.
  uint32_t getRecordStreamIndex() const { return RecordStreamIndex; }

  /// Add public symbols in bulk.
  ///
  /// \param PublicsIn Public symbols to add; ownership is taken by the builder.
  LLVM_ABI void addPublicSymbols(std::vector<BulkPublic> &&PublicsIn);

  /// Add a procedure-reference symbol to the globals stream.
  ///
  /// \param Sym The procedure-reference symbol to serialize and add.
  LLVM_ABI void addGlobalSymbol(const codeview::ProcRefSym &Sym);
  /// Add a data symbol to the globals stream.
  ///
  /// \param Sym The data symbol to serialize and add.
  LLVM_ABI void addGlobalSymbol(const codeview::DataSym &Sym);
  /// Add a constant symbol to the globals stream.
  ///
  /// \param Sym The constant symbol to serialize and add.
  LLVM_ABI void addGlobalSymbol(const codeview::ConstantSym &Sym);

  /// Add a pre-serialized global symbol record. The caller must ensure that the
  /// symbol data remains alive until the global stream is committed to disk.
  ///
  /// \param Sym The pre-serialized CodeView symbol record to add.
  LLVM_ABI void addGlobalSymbol(const codeview::CVSymbol &Sym);

private:
  void finalizePublicBuckets();
  void finalizeGlobalBuckets(uint32_t RecordZeroOffset);

  template <typename T> void serializeAndAddGlobal(const T &Symbol);

  uint32_t calculatePublicsHashStreamSize() const;
  uint32_t calculateGlobalsHashStreamSize() const;
  Error commitSymbolRecordStream(WritableBinaryStreamRef Stream);
  Error commitPublicsHashStream(WritableBinaryStreamRef Stream);
  Error commitGlobalsHashStream(WritableBinaryStreamRef Stream);

  uint32_t PublicsStreamIndex = kInvalidStreamIndex;
  uint32_t GlobalsStreamIndex = kInvalidStreamIndex;
  uint32_t RecordStreamIndex = kInvalidStreamIndex;
  msf::MSFBuilder &Msf;
  std::unique_ptr<GSIHashStreamBuilder> PSH;
  std::unique_ptr<GSIHashStreamBuilder> GSH;

  // List of all of the public records. These are stored unserialized so that we
  // can defer copying the names until we are ready to commit the PDB.
  std::vector<BulkPublic> Publics;

  // List of all of the global records.
  std::vector<codeview::CVSymbol> Globals;

  // Hash table for deduplicating global typedef and constant records. Only used
  // for globals.
  llvm::DenseSet<codeview::CVSymbol, SymbolDenseMapInfo> GlobalsSeen;
};

/// This struct is equivalent to codeview::PublicSym32, but it has been
/// optimized for size to speed up bulk serialization and sorting operations
/// during PDB writing.
struct BulkPublic {
  /// Construct a bulk public with zero flags and bucket index.
  BulkPublic() : Flags(0), BucketIdx(0) {}

  /// Pointer to the public symbol name (not necessarily null-terminated).
  const char *Name = nullptr;
  /// Length in bytes of the name pointed to by Name.
  uint32_t NameLen = 0;

  /// Offset of the symbol record in the publics stream.
  uint32_t SymOffset = 0;

  /// Section offset of the symbol in the image.
  uint32_t Offset = 0;

  /// Section index of the section containing the symbol.
  uint16_t Segment = 0;

  /// PublicSymFlags.
  uint16_t Flags : 4;

  /// GSI hash table bucket index. The maximum value is IPHR_HASH.
  uint16_t BucketIdx : 12;
  static_assert(IPHR_HASH <= 1 << 12, "bitfield too small");

  /// Set the public symbol flags from \p F.
  ///
  /// \param F The PublicSymFlags value to store in the bitfield.
  void setFlags(codeview::PublicSymFlags F) {
    Flags = uint32_t(F);
    assert(Flags == uint32_t(F) && "truncated");
  }

  /// Set the GSI hash bucket index to \p B.
  ///
  /// \param B Bucket index; must be less than IPHR_HASH.
  void setBucketIdx(uint16_t B) {
    assert(B < IPHR_HASH);
    BucketIdx = B;
  }

  /// Return the public symbol name as a StringRef.
  ///
  /// \return A StringRef view of the name pointed to by Name.
  StringRef getName() const { return StringRef(Name, NameLen); }
};

static_assert(sizeof(BulkPublic) <= 24, "unexpected size increase");
static_assert(std::is_trivially_copyable<BulkPublic>::value,
              "should be trivial");

} // namespace pdb
} // namespace llvm

#endif
