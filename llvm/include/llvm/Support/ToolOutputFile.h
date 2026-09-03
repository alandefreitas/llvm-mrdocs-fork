//===- ToolOutputFile.h - Output files for compiler-like tools --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ToolOutputFile class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TOOLOUTPUTFILE_H
#define LLVM_SUPPORT_TOOLOUTPUTFILE_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

namespace llvm {

/// Manages cleanup of a temporary tool output file.
///
/// Registers a signal handler so the file is deleted if the process is killed,
/// and deletes the file in its destructor unless \c Keep is set.
class CleanupInstaller {
public:
  /// The name of the file.
  std::string Filename;

  /// The flag which indicates whether we should not delete the file.
  bool Keep;

  /// Return the name of the file being cleaned up.
  ///
  /// \return The path of the file managed by this installer.
  StringRef getFilename() { return Filename; }
  /// Construct a cleanup installer for \p Filename.
  ///
  /// \param Filename Path of the file to delete on signal or destruction.
  LLVM_ABI explicit CleanupInstaller(StringRef Filename);
  /// Destroy the installer, deleting the file unless \c Keep is set.
  LLVM_ABI ~CleanupInstaller();
};

/// RAII wrapper around a raw_fd_ostream for compiler-like tool output files.
///
/// This class contains a raw_fd_ostream and adds a few extra features commonly
/// needed for compiler-like tool output files:
///   - The file is automatically deleted if the process is killed.
///   - The file is automatically deleted when the ToolOutputFile
///     object is destroyed unless the client calls keep().
class ToolOutputFile {
  /// This class is declared before the raw_fd_ostream so that it is constructed
  /// before the raw_fd_ostream is constructed and destructed after the
  /// raw_fd_ostream is destructed. It installs cleanups in its constructor and
  /// uninstalls them in its destructor.
  CleanupInstaller Installer;

  /// Storage for the stream, if we're owning our own stream. This is
  /// intentionally declared after Installer.
  std::optional<raw_fd_ostream> OSHolder;

  /// The actual stream to use.
  raw_fd_ostream *OS;

public:
  /// This constructor's arguments are passed to raw_fd_ostream's
  /// constructor.
  ///
  /// \param Filename Path of the file to open, or "-" for stdout.
  /// \param EC Set to an error code if opening the file fails.
  /// \param Flags Open flags forwarded to \c raw_fd_ostream.
  LLVM_ABI ToolOutputFile(StringRef Filename, std::error_code &EC,
                          sys::fs::OpenFlags Flags);

  /// Construct a ToolOutputFile from an existing file descriptor.
  ///
  /// \param Filename Path associated with the file for cleanup purposes.
  /// \param FD Open file descriptor to wrap in a \c raw_fd_ostream.
  LLVM_ABI ToolOutputFile(StringRef Filename, int FD);

  /// Return the contained raw_fd_ostream.
  ///
  /// \return A reference to the underlying output stream.
  raw_fd_ostream &os() { return *OS; }

  /// Return the filename initialized with.
  ///
  /// \return The path of the output file.
  StringRef getFilename() { return Installer.getFilename(); }

  /// Indicate that the tool's job wrt this output file has been successful and
  /// the file should not be deleted.
  void keep() { Installer.Keep = true; }

  /// Return the output filename as a string reference.
  ///
  /// \return The path of the output file as a \c std::string.
  const std::string &outputFilename() { return Installer.Filename; }
};

} // end llvm namespace

#endif
