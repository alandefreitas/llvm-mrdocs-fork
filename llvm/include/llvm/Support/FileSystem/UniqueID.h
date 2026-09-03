//===- llvm/Support/FileSystem/UniqueID.h - UniqueID for files --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is cut out of llvm/Support/FileSystem.h to allow UniqueID to be
// reused without bloating the includes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FILESYSTEM_UNIQUEID_H
#define LLVM_SUPPORT_FILESYSTEM_UNIQUEID_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include <cstdint>
#include <utility>

namespace llvm {
namespace sys {
namespace fs {

/// Stable identifier for a filesystem object, composed of device and inode.
class UniqueID {
  uint64_t Device;
  uint64_t File;

public:
  /// Construct an empty unique ID.
  UniqueID() = default;
  /// Construct from device number \p Device and file number \p File.
  ///
  /// \param Device Device number of the filesystem object.
  /// \param File Inode or file number of the filesystem object.
  UniqueID(uint64_t Device, uint64_t File) : Device(Device), File(File) {}

  /// Return true if both IDs identify the same filesystem object.
  ///
  /// \param Other Unique ID to compare against.
  /// \return True if both IDs identify the same filesystem object.
  bool operator==(const UniqueID &Other) const {
    return Device == Other.Device && File == Other.File;
  }
  /// Return true if the IDs identify different filesystem objects.
  ///
  /// \param Other Unique ID to compare against.
  /// \return True if the IDs identify different filesystem objects.
  bool operator!=(const UniqueID &Other) const { return !(*this == Other); }
  /// Order UniqueIDs by device then inode for use in ordered containers.
  ///
  /// \param Other Unique ID to compare against.
  /// \return True if this UniqueID orders before \p Other by device then inode.
  bool operator<(const UniqueID &Other) const {
    /// Don't use std::tie since it bloats the compile time of this header.
    if (Device < Other.Device)
      return true;
    if (Other.Device < Device)
      return false;
    return File < Other.File;
  }

  /// Return the device component of the unique ID.
  ///
  /// \return The device component of the unique ID.
  uint64_t getDevice() const { return Device; }
  /// Return the file/inode component of the unique ID.
  ///
  /// \return The file/inode component of the unique ID.
  uint64_t getFile() const { return File; }
};

} // end namespace fs
} // end namespace sys

/// DenseMapInfo specialization so UniqueID can be used as a DenseMap key.
template <> struct DenseMapInfo<llvm::sys::fs::UniqueID> {
  /// Compute a hash value for unique ID \p Tag.
  ///
  /// \param Tag Unique ID to hash.
  /// \return A hash code derived from \p Tag's device and file components.
  static hash_code getHashValue(const llvm::sys::fs::UniqueID &Tag) {
    return hash_value(std::make_pair(Tag.getDevice(), Tag.getFile()));
  }

  /// Return true if \p LHS and \p RHS identify the same filesystem object.
  ///
  /// \param LHS Left-hand unique ID.
  /// \param RHS Right-hand unique ID.
  /// \return True if \p LHS and \p RHS identify the same filesystem object.
  static bool isEqual(const llvm::sys::fs::UniqueID &LHS,
                      const llvm::sys::fs::UniqueID &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

#endif // LLVM_SUPPORT_FILESYSTEM_UNIQUEID_H
