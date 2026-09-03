//===- llvm/Object/BuildID.h - Build ID -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares a library for handling Build IDs and using them to find
/// debug info.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_OBJECT_BUILDID_H
#define LLVM_DEBUGINFO_OBJECT_BUILDID_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace object {

/// A build ID in binary form.
typedef SmallVector<uint8_t, 10> BuildID;

/// A reference to a BuildID in binary form.
typedef ArrayRef<uint8_t> BuildIDRef;

class ObjectFile;

/// Parses a build ID from a hex string.
/// \param Str Hex string representation of a build ID.
/// \return Binary build ID parsed from the hex string.
LLVM_ABI BuildID parseBuildID(StringRef Str);

/// Returns the build ID, if any, contained in the given object file.
/// \param Obj Object file to inspect for a build ID.
/// \return Build ID bytes from the object, or an empty reference if none.
LLVM_ABI BuildIDRef getBuildID(const ObjectFile *Obj);

/// BuildIDFetcher searches local cache directories for debug info.
class LLVM_ABI BuildIDFetcher {
public:
  /// Construct a BuildIDFetcher that searches the given directories.
  /// \param DebugFileDirectories Paths to search for debug info files.
  BuildIDFetcher(std::vector<std::string> DebugFileDirectories)
      : DebugFileDirectories(std::move(DebugFileDirectories)) {}
  /// Virtual destructor for polymorphic BuildIDFetcher subclasses.
  virtual ~BuildIDFetcher() = default;

  /// Returns the path to the debug file with the given build ID.
  /// \param BuildID Build ID of the debug file to locate.
  /// \return Path to the matching debug file, or an error if none is found.
  virtual Expected<std::string> fetch(BuildIDRef BuildID) const;

private:
  const std::vector<std::string> DebugFileDirectories;
};

} // namespace object
} // namespace llvm

#endif // LLVM_DEBUGINFO_OBJECT_BUILDID_H
