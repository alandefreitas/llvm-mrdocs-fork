//===- llvm/Support/FileUtilities.h - File System Utilities -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a family of utility functions which are useful for doing
// various things with files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FILEUTILITIES_H
#define LLVM_SUPPORT_FILEUTILITIES_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"

#include <system_error>

namespace llvm {

/// Compare two files, allowing absolute and relative floating-point tolerance.
///
/// Returns 0 if the files match, 1 if they are different, and 2 if there is a
/// file error. Absolute and relative FP error may be specified; if they differ
/// by more than those tolerances, the files are considered different. If Error
/// is non-null, it is set to an error message when an error occurs or the files
/// differ.
///
/// \param FileA Path of the first file to compare.
/// \param FileB Path of the second file to compare.
/// \param AbsTol Absolute floating-point difference allowed between values.
/// \param RelTol Relative floating-point difference allowed between values.
/// \param Error Optional string filled with an error or difference message.
/// \return 0 if the files match, 1 if they differ, and 2 on file error.
LLVM_ABI int DiffFilesWithTolerance(StringRef FileA, StringRef FileB,
                                    double AbsTol, double RelTol,
                                    std::string *Error = nullptr);

/// RAII helper that deletes a file when destroyed, typically on exceptions.
///
/// This class is a simple object meant to be stack allocated. If an exception
/// is thrown from a region, the object removes the filename specified (if
/// deleteIt is true).
class FileRemover {
  SmallString<128> Filename;
  bool DeleteIt;

public:
  /// Construct a FileRemover that owns no file.
  FileRemover() : DeleteIt(false) {}

  /// Construct a FileRemover that optionally deletes \p filename on destruction.
  ///
  /// \param filename Path of the file to delete when this object is destroyed.
  /// \param deleteIt If true, delete the file in the destructor.
  explicit FileRemover(const Twine &filename, bool deleteIt = true)
      : DeleteIt(deleteIt) {
    filename.toVector(Filename);
  }

  /// Destroy the FileRemover, deleting the owned file if deleteIt is true.
  ~FileRemover() {
    if (DeleteIt) {
      // Ignore problems deleting the file.
      sys::fs::remove(Filename);
    }
  }

  /// Take ownership of a file so it is removed when this object is destroyed.
  ///
  /// If the FileRemover already had ownership of a file, remove it first.
  ///
  /// \param filename Path of the file to own and optionally delete.
  /// \param deleteIt If true, delete the file when this object is destroyed.
  void setFile(const Twine &filename, bool deleteIt = true) {
    if (DeleteIt) {
      // Ignore problems deleting the file.
      sys::fs::remove(Filename);
    }

    Filename.clear();
    filename.toVector(Filename);
    DeleteIt = deleteIt;
  }

  /// releaseFile - Take ownership of the file away from the FileRemover so it
  /// will not be removed when the object is destroyed.
  void releaseFile() { DeleteIt = false; }
};

/// Copies permissions (and optionally dates) from an input file to an output.
///
/// FilePermissionsApplier helps to copy permissions from an input file to an
/// output one. It memorizes the status of the input file and can apply
/// permissions and dates to the output file.
class FilePermissionsApplier {
public:
  /// Create an applier that memorizes permissions from \p InputFilename.
  ///
  /// \param InputFilename Path of the file whose status is memorized.
  /// \return An Expected holding the applier, or an error if status cannot be
  /// read.
  LLVM_ABI static Expected<FilePermissionsApplier>
  create(StringRef InputFilename);

  /// Apply memorized permissions to an output file.
  ///
  /// Copy LastAccess and ModificationTime if \p CopyDates is true. Overwrite
  /// stored permissions if \p OverwritePermissions is specified.
  ///
  /// \param OutputFilename Path of the file to receive the permissions.
  /// \param CopyDates If true, also copy last-access and modification times.
  /// \param OverwritePermissions Optional permissions that replace the stored
  /// ones when applying.
  /// \return Error::success() on success, or an Error describing the failure.
  LLVM_ABI Error
  apply(StringRef OutputFilename, bool CopyDates = false,
        std::optional<sys::fs::perms> OverwritePermissions = std::nullopt);

private:
  FilePermissionsApplier(StringRef InputFilename, sys::fs::file_status Status)
      : InputFilename(InputFilename), InputStatus(Status) {}

  StringRef InputFilename;
  sys::fs::file_status InputStatus;
};
} // namespace llvm

#endif
