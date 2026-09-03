//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations of the concrete VirtualOutputBackend
/// classes, which are the implementation for different output style and
/// functions. This file contains:
/// * NullOutputBackend: discard all outputs written.
/// * OnDiskOutputBackend: write output to disk, with support for common output
/// types, like append, or atomic update.
/// * FilteringOutputBackend: filer some output paths to underlying output
/// backend.
/// * MirrorOutputBackend: mirror output to two output backends.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_VIRTUALOUTPUTBACKENDS_H
#define LLVM_SUPPORT_VIRTUALOUTPUTBACKENDS_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/VirtualOutputBackend.h"
#include "llvm/Support/VirtualOutputConfig.h"

namespace llvm::vfs {

/// Create a backend that ignores all output.
/// \returns A backend that discards every createFile() request.
LLVM_ABI IntrusiveRefCntPtr<OutputBackend> makeNullOutputBackend();

/// Make a backend where \a OutputBackend::createFile() forwards to
/// \p UnderlyingBackend when \p Filter is true, and otherwise returns a
/// \a NullOutput.
/// \param UnderlyingBackend Backend that receives createFile() when the
/// filter accepts the path.
/// \param Filter Predicate that returns true when \p UnderlyingBackend
/// should handle the file.
/// \returns A filtering backend wrapping \p UnderlyingBackend.
LLVM_ABI IntrusiveRefCntPtr<OutputBackend> makeFilteringOutputBackend(
    IntrusiveRefCntPtr<OutputBackend> UnderlyingBackend,
    std::function<bool(StringRef, std::optional<OutputConfig>)> Filter);

/// Create a backend that mirrors outputs to two backends.
///
/// Forwards \a OutputBackend::createFile() to both \p Backend1 and \p
/// Backend2. Writing to such backend will create identical outputs using
/// two different backends.
/// \param Backend1 First backend to receive createFile() calls.
/// \param Backend2 Second backend to receive createFile() calls.
/// \returns A backend that mirrors createFile() to both backends.
LLVM_ABI IntrusiveRefCntPtr<OutputBackend>
makeMirroringOutputBackend(IntrusiveRefCntPtr<OutputBackend> Backend1,
                           IntrusiveRefCntPtr<OutputBackend> Backend2);

/// A helper class for proxying another backend, with the default
/// implementation to forward to the underlying backend.
class LLVM_ABI ProxyOutputBackend : public OutputBackend {
  void anchor() override;

protected:
  // Require subclass to implement cloneImpl().
  //
  // IntrusiveRefCntPtr<OutputBackend> cloneImpl() const override;

  /// Create a file by forwarding to the underlying backend.
  /// \param Path Path of the file to create.
  /// \param Config Optional output configuration for the file.
  /// \returns The created file implementation, or an error.
  Expected<std::unique_ptr<OutputFileImpl>>
  createFileImpl(StringRef Path, std::optional<OutputConfig> Config) override {
    OutputFile File;
    if (Error E = UnderlyingBackend->createFile(Path, Config).moveInto(File))
      return std::move(E);
    return File.takeImpl();
  }

  /// Return the backend that this proxy forwards to.
  /// \returns The underlying output backend.
  OutputBackend &getUnderlyingBackend() const { return *UnderlyingBackend; }

public:
  /// Construct a proxy around \p UnderlyingBackend.
  /// \param UnderlyingBackend Non-null backend to forward operations to.
  ProxyOutputBackend(IntrusiveRefCntPtr<OutputBackend> UnderlyingBackend)
      : UnderlyingBackend(std::move(UnderlyingBackend)) {
    assert(this->UnderlyingBackend && "Expected non-null backend");
  }

private:
  IntrusiveRefCntPtr<OutputBackend> UnderlyingBackend;
};

/// An output backend that creates files on disk, wrapping APIs in sys::fs.
class LLVM_ABI OnDiskOutputBackend : public OutputBackend {
  void anchor() override;

protected:
  /// Clone this backend, copying its on-disk settings.
  /// \returns A new on-disk backend with a copy of this backend's settings.
  IntrusiveRefCntPtr<OutputBackend> cloneImpl() const override {
    return clone();
  }

  /// Create a file on disk for \p Path.
  /// \param Path Path of the file to create.
  /// \param Config Optional output configuration for the file.
  /// \returns The created file implementation, or an error.
  Expected<std::unique_ptr<OutputFileImpl>>
  createFileImpl(StringRef Path, std::optional<OutputConfig> Config) override;

public:
  /// Resolve an absolute path.
  /// \param Path Path buffer updated in place to an absolute path.
  /// \returns Success, or an error if the path could not be made absolute.
  Error makeAbsolute(SmallVectorImpl<char> &Path) const;

  /// On disk output settings.
  struct OutputSettings {
    /// Register output files to be deleted if a signal is received. Also
    /// enabled for outputs with \a OutputConfig::getDiscardOnSignal().
    bool RemoveOnSignal = true;

    /// Use temporary files. Also enabled for outputs with \a
    /// OutputConfig::getAtomicWrite().
    bool UseTemporaries = true;

    /// Default configuration for this backend.
    OutputConfig DefaultConfig;
  };

  /// Clone this backend with a copy of its settings.
  /// \returns A new on-disk backend with a copy of this backend's settings.
  IntrusiveRefCntPtr<OnDiskOutputBackend> clone() const {
    auto Clone = makeIntrusiveRefCnt<OnDiskOutputBackend>();
    Clone->Settings = Settings;
    return Clone;
  }

  /// Construct an on-disk backend with default settings.
  OnDiskOutputBackend() = default;

  /// Settings for this backend.
  ///
  /// Access is not thread-safe.
  OutputSettings Settings;
};

} // namespace llvm::vfs

#endif // LLVM_SUPPORT_VIRTUALOUTPUTBACKENDS_H
