//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations of the HashingOutputBackend class, which
/// is the VirtualOutputBackend that only produces the hashes for the output
/// files. This is useful for checking if the outputs are deterministic without
/// storing output files in memory or on disk.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_HASHINGOUTPUTBACKEND_H
#define LLVM_SUPPORT_HASHINGOUTPUTBACKEND_H

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/HashBuilder.h"
#include "llvm/Support/VirtualOutputBackend.h"
#include "llvm/Support/VirtualOutputConfig.h"
#include "llvm/Support/raw_ostream.h"
#include <mutex>

namespace llvm::vfs {

/// raw_pwrite_stream that writes to a hasher.
template <typename HasherT>
class HashingStream : public llvm::raw_pwrite_stream {
private:
  SmallVector<char> Buffer;
  raw_svector_ostream OS;

  using HashBuilderT = HashBuilder<HasherT, endianness::native>;
  HashBuilderT Builder;

  void write_impl(const char *Ptr, size_t Size) override {
    OS.write(Ptr, Size);
  }

  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override {
    OS.pwrite(Ptr, Size, Offset);
  }

  uint64_t current_pos() const override { return OS.str().size(); }

public:
  /// Construct an unbuffered stream that hashes written data.
  HashingStream() : OS(Buffer) { SetUnbuffered(); }

  /// Finalize the hash of all written data and return the digest.
  ///
  /// \return The hash digest of all data written to the stream.
  auto final() {
    Builder.update(OS.str());
    return Builder.final();
  }
};

template <typename HasherT> class HashingOutputFile;

/// An output backend that only generates the hash for outputs.
template <typename HasherT> class HashingOutputBackend : public OutputBackend {
private:
  friend class HashingOutputFile<HasherT>;
  void addOutputFile(StringRef Path, StringRef Hash) {
    std::lock_guard<std::mutex> Lock(OutputHashLock);
    OutputHashes[Path] = Hash.str();
  }

protected:
  /// Return this backend; hashing backends share state and are not cloned.
  ///
  /// \return A reference-counted pointer to this backend.
  IntrusiveRefCntPtr<OutputBackend> cloneImpl() const override {
    return const_cast<HashingOutputBackend<HasherT> *>(this);
  }

  /// Create a hashing output file for \p Path.
  ///
  /// \param Path Output path whose contents will be hashed.
  /// \param Config Optional output configuration; unused by this backend.
  /// \return A hashing output file for \p Path, or an error on failure.
  Expected<std::unique_ptr<OutputFileImpl>>
  createFileImpl(StringRef Path, std::optional<OutputConfig> Config) override {
    return std::make_unique<HashingOutputFile<HasherT>>(Path, *this);
  }

public:
  /// Iterator for all the output file names.
  ///
  /// Not thread safe. Should be queried after all outputs are written.
  ///
  /// \return A range of the hashed output file paths.
  auto outputFiles() const { return OutputHashes.keys(); }

  /// Get hash value for the output files in hex representation.
  ///
  /// Not thread safe. Should be queried after all outputs are written.
  ///
  /// \param Path Output path whose hash should be returned.
  /// \return The hex-encoded hash for \p Path, or \c std::nullopt if that
  /// path was not generated.
  std::optional<std::string> getHashValueForFile(StringRef Path) {
    auto F = OutputHashes.find(Path);
    if (F == OutputHashes.end())
      return std::nullopt;
    return toHex(F->second);
  }

private:
  std::mutex OutputHashLock;
  StringMap<std::string> OutputHashes;
};

/// HashingOutputFile.
template <typename HasherT>
class HashingOutputFile final : public OutputFileImpl {
public:
  /// Finalize the content hash and store it in the backend.
  ///
  /// \return Success, or an error if the hash could not be stored.
  Error keep() override {
    auto Result = OS.final();
    Backend.addOutputFile(OutputPath, toStringRef(Result));
    return Error::success();
  }
  /// Discard the output without recording a hash.
  ///
  /// \return Success.
  Error discard() override { return Error::success(); }
  /// Return the stream that hashes written content.
  ///
  /// \return The hashing stream that receives written content.
  raw_pwrite_stream &getOS() override { return OS; }

  /// Construct a hashing output file for \p OutputPath owned by \p Backend.
  ///
  /// \param OutputPath Path of the logical output whose contents are hashed.
  /// \param Backend Backend that will store the hash when the file is kept.
  HashingOutputFile(StringRef OutputPath,
                    HashingOutputBackend<HasherT> &Backend)
      : OutputPath(OutputPath.str()), Backend(Backend) {}

private:
  const std::string OutputPath;
  HashingStream<HasherT> OS;
  HashingOutputBackend<HasherT> &Backend;
};

} // namespace llvm::vfs

#endif // LLVM_SUPPORT_HASHINGOUTPUTBACKEND_H
