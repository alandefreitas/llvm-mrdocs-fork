//===-- llvm/Support/TarWriter.h - Tar archive file creator -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TARWRITER_H
#define LLVM_SUPPORT_TARWRITER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
/// Creates Unix tar archive files.
class TarWriter {
public:
  /// Creates a TarWriter that writes a new archive to \p OutputPath.
  ///
  /// \param OutputPath Path of the tar archive file to create.
  /// \param BaseDir Directory prefix prepended to paths written into the
  ///        archive.
  /// \returns A TarWriter on success, or an Error if the file cannot be opened.
  LLVM_ABI static Expected<std::unique_ptr<TarWriter>>
  create(StringRef OutputPath, StringRef BaseDir);

  /// Appends a file with path \p Path and contents \p Data to the archive.
  ///
  /// Duplicate paths are ignored. Paths that do not fit in a Ustar header use
  /// the PAX extension.
  ///
  /// \param Path Relative path of the file within the archive.
  /// \param Data Contents of the file to append.
  LLVM_ABI void append(StringRef Path, StringRef Data);

private:
  TarWriter(int FD, StringRef BaseDir);
  raw_fd_ostream OS;
  std::string BaseDir;
  StringSet<> Files;
};
}

#endif
