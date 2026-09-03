//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations of the llvm::vfs::OutputFile class,
/// which is a virtualized output file from output backend. \c OutputFile can be
/// use a \c raw_pwrite_stream for writing, and are required to be `keep()` or
/// `discard()` in the end.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_VIRTUALOUTPUTFILE_H
#define LLVM_SUPPORT_VIRTUALOUTPUTFILE_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ExtensibleRTTI.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm::vfs {

/// Abstract backend implementation for a virtualized output file.
class LLVM_ABI OutputFileImpl : public RTTIExtends<OutputFileImpl, RTTIRoot> {
  void anchor() override;

public:
  /// RTTI identifier for OutputFileImpl.
  static char ID;
  /// Destroy the implementation.
  ~OutputFileImpl() override = default;

  /// Commit the output and close it.
  /// \returns Success, or an error if commit fails.
  virtual Error keep() = 0;
  /// Discard the output and clean up temporary state.
  /// \returns Success, or an error if clean-up fails.
  virtual Error discard() = 0;
  /// Return the underlying write stream.
  /// \returns The underlying write stream.
  virtual raw_pwrite_stream &getOS() = 0;
};

/// An \a OutputFileImpl that discards all written data.
class LLVM_ABI NullOutputFileImpl final
    : public RTTIExtends<NullOutputFileImpl, OutputFileImpl> {
  void anchor() override;

public:
  /// RTTI identifier for NullOutputFileImpl.
  static char ID;
  /// Commit the null output successfully.
  /// \returns Success.
  Error keep() final { return Error::success(); }
  /// Discard the null output successfully.
  /// \returns Success.
  Error discard() final { return Error::success(); }
  /// Return the underlying null write stream.
  /// \returns The underlying null write stream.
  raw_pwrite_stream &getOS() final { return OS; }

private:
  raw_null_ostream OS;
};

/// A virtualized output file that writes to a specific backend.
///
/// One of \a keep(), \a discard(), or \a discardOnDestroy() must be called
/// before destruction.
class OutputFile {
public:
  /// Return the path of this output file.
  /// \returns The path of this output file.
  StringRef getPath() const { return Path; }

  /// Check if \a keep() or \a discard() has already been called.
  /// \returns True if the file is still open.
  bool isOpen() const { return bool(Impl); }

  /// Return true if the file is still open.
  /// \returns True if the file is still open.
  explicit operator bool() const { return isOpen(); }

  /// Return the underlying write stream.
  /// \returns The underlying write stream.
  raw_pwrite_stream &getOS() {
    assert(isOpen() && "Expected open output stream");
    return Impl->getOS();
  }
  /// Convert to a reference to the underlying write stream.
  /// \returns A reference to the underlying write stream.
  operator raw_pwrite_stream &() { return getOS(); }
  /// Write \p V to the underlying stream.
  /// \param V Value to write.
  /// \returns The underlying stream.
  template <class T> raw_ostream &operator<<(T &&V) {
    return getOS() << std::forward<T>(V);
  }

  /// Keep an output. Errors if this fails.
  ///
  /// If it has already been closed, calls \a report_fatal_error().
  ///
  /// If there's an open proxy from \a createProxy(), calls \a discard() to
  /// clean up temporaries followed by \a report_fatal_error().
  /// \returns Success, or an error if keep fails.
  LLVM_ABI Error keep();

  /// Discard an output, cleaning up any temporary state. Errors if clean-up
  /// fails.
  ///
  /// If it has already been closed, calls \a report_fatal_error().
  /// \returns Success, or an error if clean-up fails.
  LLVM_ABI Error discard();

  /// Discard the output when destroying it if it's still open, sending the
  /// result to \p Handler.
  /// \param Handler Callback invoked with the discard result.
  void discardOnDestroy(unique_function<void(Error E)> Handler) {
    DiscardOnDestroyHandler = std::move(Handler);
  }

  /// Create a proxy stream for clients that need an owned stream.
  ///
  /// Errors if there's already a proxy. The proxy must be deleted before
  /// calling \a keep(). The proxy will crash if it's written to after calling
  /// \a discard().
  /// \returns A proxy stream, or an error if one already exists.
  LLVM_ABI Expected<std::unique_ptr<raw_pwrite_stream>> createProxy();

  /// Return true if a proxy from \a createProxy() is still open.
  /// \returns True if a proxy from \a createProxy() is still open.
  bool hasOpenProxy() const { return OpenProxy; }

  /// Take the implementation.
  ///
  /// \pre \a hasOpenProxy() is false.
  /// \pre \a discardOnDestroy() has not been called.
  /// \returns The implementation, leaving this file closed.
  std::unique_ptr<OutputFileImpl> takeImpl() {
    assert(!hasOpenProxy() && "Unexpected open proxy");
    assert(!DiscardOnDestroyHandler && "Unexpected discard handler");
    return std::move(Impl);
  }

  /// Check whether this is a null output file.
  /// \returns True if this is a null output file.
  bool isNull() const { return Impl && isa<NullOutputFileImpl>(*Impl); }

  /// Construct a closed output file.
  OutputFile() = default;

  /// Construct an open output file for \p Path backed by \p Impl.
  /// \param Path Path of the output.
  /// \param Impl Backend-specific implementation. Must be non-null.
  explicit OutputFile(const Twine &Path, std::unique_ptr<OutputFileImpl> Impl)
      : Path(Path.str()), Impl(std::move(Impl)) {
    assert(this->Impl && "Expected open output file");
  }

  /// Destroy the file, discarding if still open per \a discardOnDestroy().
  ~OutputFile() { destroy(); }
  /// Move-construct from \p O.
  /// \param O Output file to move from.
  OutputFile(OutputFile &&O) { moveFrom(O); }
  /// Move-assign from \p O.
  /// \param O Output file to move from.
  /// \returns *this.
  OutputFile &operator=(OutputFile &&O) {
    destroy();
    return moveFrom(O);
  }

private:
  /// Destroy \a Impl. Reports fatal error if the file is open and there's no
  /// handler from \a discardOnDestroy().
  LLVM_ABI void destroy();
  OutputFile &moveFrom(OutputFile &O) {
    Path = std::move(O.Path);
    Impl = std::move(O.Impl);
    DiscardOnDestroyHandler = std::move(O.DiscardOnDestroyHandler);
    OpenProxy = O.OpenProxy;
    O.OpenProxy = nullptr;
    return *this;
  }

  std::string Path;
  std::unique_ptr<OutputFileImpl> Impl;
  unique_function<void(Error E)> DiscardOnDestroyHandler;

  class TrackedProxy;
  TrackedProxy *OpenProxy = nullptr;
};

/// Update \p File to silently discard itself if it's still open when it's
/// destroyed.
/// \param File Output file to update.
inline void consumeDiscardOnDestroy(OutputFile &File) {
  File.discardOnDestroy(consumeError);
}

/// Update \p File to silently discard itself if it's still open when it's
/// destroyed.
/// \param File Expected output file to update when valid.
/// \returns The (possibly updated) \p File.
inline Expected<OutputFile> consumeDiscardOnDestroy(Expected<OutputFile> File) {
  if (File)
    consumeDiscardOnDestroy(*File);
  return File;
}

} // namespace llvm::vfs

#endif // LLVM_SUPPORT_VIRTUALOUTPUTFILE_H
