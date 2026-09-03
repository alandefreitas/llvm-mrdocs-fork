//===- MachOUniversalWriter.h - MachO universal binary writer----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the Slice class and writeUniversalBinary function for writing a
// MachO universal binary file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_MACHOUNIVERSALWRITER_H
#define LLVM_OBJECT_MACHOUNIVERSALWRITER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>

namespace llvm {
class LLVMContext;

namespace object {
class Archive;
class Binary;
class IRObjectFile;
class MachOObjectFile;

/// One architecture slice to include when writing a Mach-O universal binary.
class Slice {
  const Binary *B;
  uint32_t CPUType;
  uint32_t CPUSubType;
  std::string ArchName;

  // P2Alignment field stores slice alignment values from universal
  // binaries. This is also needed to order the slices so the total
  // file size can be calculated before creating the output buffer.
  uint32_t P2Alignment;

  Slice(const IRObjectFile &IRO, uint32_t CPUType, uint32_t CPUSubType,
        std::string ArchName, uint32_t Align);

public:
  /// Construct a slice from a Mach-O object, inferring architecture metadata.
  ///
  /// \param O Mach-O object file that forms this slice.
  LLVM_ABI explicit Slice(const MachOObjectFile &O);

  /// Construct a slice from a Mach-O object with an explicit alignment.
  ///
  /// \param O Mach-O object file that forms this slice.
  /// \param Align Log2 of the slice alignment in bytes.
  LLVM_ABI Slice(const MachOObjectFile &O, uint32_t Align);

  /// Construct a slice from an archive with explicit architecture metadata.
  ///
  /// Architecture fields are taken from the caller instead of being inferred
  /// from the archive members.
  ///
  /// \param A Archive whose contents form this slice.
  /// \param CPUType Mach-O CPU type for the slice.
  /// \param CPUSubType Mach-O CPU subtype for the slice.
  /// \param ArchName Architecture name string for the slice.
  /// \param Align Log2 of the slice alignment in bytes.
  LLVM_ABI Slice(const Archive &A, uint32_t CPUType, uint32_t CPUSubType,
                 std::string ArchName, uint32_t Align);

  /// Create a slice from an archive, optionally using bitcode members.
  ///
  /// \param A Archive to wrap as a universal-binary slice.
  /// \param LLVMCtx Optional LLVM IR context used when archive members are
  ///        bitcode; may be null.
  /// \return The new slice, or an error if creation fails.
  LLVM_ABI static Expected<Slice> create(const Archive &A,
                                         LLVMContext *LLVMCtx = nullptr);

  /// Create a slice from an IR object file with an explicit alignment.
  ///
  /// \param IRO IR object file that forms this slice.
  /// \param Align Log2 of the slice alignment in bytes.
  /// \return The new slice, or an error if creation fails.
  LLVM_ABI static Expected<Slice> create(const IRObjectFile &IRO,
                                         uint32_t Align);

  /// Set the log2 alignment of this slice in the universal binary.
  ///
  /// \param Align Log2 of the slice alignment in bytes.
  void setP2Alignment(uint32_t Align) { P2Alignment = Align; }

  /// Return the binary that forms this slice.
  ///
  /// \return The binary that forms this slice.
  const Binary *getBinary() const { return B; }

  /// Return the Mach-O CPU type of this slice.
  ///
  /// \return The Mach-O CPU type of this slice.
  uint32_t getCPUType() const { return CPUType; }

  /// Return the Mach-O CPU subtype of this slice.
  ///
  /// \return The Mach-O CPU subtype of this slice.
  uint32_t getCPUSubType() const { return CPUSubType; }

  /// Return the log2 alignment of this slice in the universal binary.
  ///
  /// \return The log2 alignment of this slice in the universal binary.
  uint32_t getP2Alignment() const { return P2Alignment; }

  /// Return a 64-bit key combining the CPU type and subtype.
  ///
  /// \return A 64-bit key combining the CPU type and subtype.
  uint64_t getCPUID() const {
    return static_cast<uint64_t>(CPUType) << 32 | CPUSubType;
  }

  /// Return the architecture name, or a fallback for unknown CPU ids.
  ///
  /// \return The architecture name, or a fallback for unknown CPU ids.
  std::string getArchString() const {
    if (!ArchName.empty())
      return ArchName;
    return ("unknown(" + Twine(CPUType) + "," +
            Twine(CPUSubType & ~MachO::CPU_SUBTYPE_MASK) + ")")
        .str();
  }

  /// Compare slices for ordering in a universal binary.
  ///
  /// Arm64-family slices sort after other CPU types for cctools lipo
  /// compatibility; otherwise slices are ordered by CPU type, subtype, and
  /// alignment to minimize file size.
  ///
  /// \param Lhs Left-hand slice.
  /// \param Rhs Right-hand slice.
  /// \return True if \p Lhs should be ordered before \p Rhs.
  friend bool operator<(const Slice &Lhs, const Slice &Rhs) {
    if (Lhs.CPUType == Rhs.CPUType)
      return Lhs.CPUSubType < Rhs.CPUSubType;
    // force arm64-family to follow after all other slices for
    // compatibility with cctools lipo
    if (Lhs.CPUType == MachO::CPU_TYPE_ARM64)
      return false;
    if (Rhs.CPUType == MachO::CPU_TYPE_ARM64)
      return true;
    // Sort by alignment to minimize file size
    return Lhs.P2Alignment < Rhs.P2Alignment;
  }
};

/// Kind of fat header to emit in a Mach-O universal binary.
enum class FatHeaderType {
  FatHeader,   ///< 32-bit fat header (FAT_MAGIC).
  Fat64Header, ///< 64-bit fat header (FAT_MAGIC_64).
};

/// Write a Mach-O universal binary containing \p Slices to \p OutputFileName.
///
/// \param Slices Architecture slices to include in the universal binary.
/// \param OutputFileName Path of the file to write.
/// \param FatHeader Whether to emit a 32-bit or 64-bit fat header.
/// \return Error::success() on success, or an error if writing fails.
LLVM_ABI Error
writeUniversalBinary(ArrayRef<Slice> Slices, StringRef OutputFileName,
                     FatHeaderType FatHeader = FatHeaderType::FatHeader);

/// Write a Mach-O universal binary containing \p Slices to \p Out.
///
/// \param Slices Architecture slices to include in the universal binary.
/// \param Out Stream to receive the universal binary bytes.
/// \param FatHeader Whether to emit a 32-bit or 64-bit fat header.
/// \return Error::success() on success, or an error if writing fails.
LLVM_ABI Error writeUniversalBinaryToStream(
    ArrayRef<Slice> Slices, raw_ostream &Out,
    FatHeaderType FatHeader = FatHeaderType::FatHeader);

} // end namespace object

} // end namespace llvm

#endif // LLVM_OBJECT_MACHOUNIVERSALWRITER_H
