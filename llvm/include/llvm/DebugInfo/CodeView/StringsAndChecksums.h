//===- StringsAndChecksums.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_STRINGSANDCHECKSUMS_H
#define LLVM_DEBUGINFO_CODEVIEW_STRINGSANDCHECKSUMS_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {
namespace codeview {
class DebugChecksumsSubsection;
class DebugChecksumsSubsectionRef;
class DebugStringTableSubsection;
class DebugStringTableSubsectionRef;

/// Holds non-owning references to CodeView string-table and checksums
/// subsections, discovering either from a fragment range when needed.
class StringsAndChecksumsRef {
public:
  /// Construct a reference with no known string table or checksums subsections.
  ///
  /// If no subsections are known about initially, we find as much as we can.
  LLVM_ABI StringsAndChecksumsRef();

  /// Construct a reference that already knows the string table subsection.
  ///
  /// If only a string table subsection is given, we find a checksums subsection.
  ///
  /// \param Strings Known string table subsection reference.
  LLVM_ABI explicit StringsAndChecksumsRef(
      const DebugStringTableSubsectionRef &Strings);

  /// Construct a reference that already knows both subsections.
  ///
  /// If both subsections are given, we don't need to find anything.
  ///
  /// \param Strings Known string table subsection reference.
  /// \param Checksums Known file checksums subsection reference.
  LLVM_ABI StringsAndChecksumsRef(const DebugStringTableSubsectionRef &Strings,
                                  const DebugChecksumsSubsectionRef &Checksums);

  /// Set the string table subsection reference to \p Strings.
  ///
  /// \param Strings String table subsection to associate with this object.
  LLVM_ABI void setStrings(const DebugStringTableSubsectionRef &Strings);
  /// Set the file checksums subsection reference to \p CS.
  ///
  /// \param CS File checksums subsection to associate with this object.
  LLVM_ABI void setChecksums(const DebugChecksumsSubsectionRef &CS);

  /// Clear both the string table and checksums subsection references.
  LLVM_ABI void reset();
  /// Clear only the string table subsection reference.
  LLVM_ABI void resetStrings();
  /// Clear only the file checksums subsection reference.
  LLVM_ABI void resetChecksums();

  /// Discover string table and checksums subsections from \p FragmentRange.
  ///
  /// Walks the given fragment range and initializes the string table and/or
  /// checksums references from matching subsection records that have not yet
  /// been set. Stops early once both subsections are known.
  ///
  /// \param FragmentRange Range of debug subsection records to scan.
  template <typename T> void initialize(T &&FragmentRange) {
    for (const DebugSubsectionRecord &R : FragmentRange) {
      if (Strings && Checksums)
        return;
      if (R.kind() == DebugSubsectionKind::FileChecksums) {
        initializeChecksums(R);
        continue;
      }
      if (R.kind() == DebugSubsectionKind::StringTable && !Strings) {
        // While in practice we should never encounter a string table even
        // though the string table is already initialized, in theory it's
        // possible.  PDBs are supposed to have one global string table and
        // then this subsection should not appear.  Whereas object files are
        // supposed to have this subsection appear exactly once.  However,
        // for testing purposes it's nice to be able to test this subsection
        // independently of one format or the other, so for some tests we
        // manually construct a PDB that contains this subsection in addition
        // to a global string table.
        initializeStrings(R);
        continue;
      }
    }
  }

  /// Return the associated string table subsection reference.
  ///
  /// \returns The associated string table subsection reference.
  const DebugStringTableSubsectionRef &strings() const { return *Strings; }
  /// Return the associated file checksums subsection reference.
  ///
  /// \returns The associated file checksums subsection reference.
  const DebugChecksumsSubsectionRef &checksums() const { return *Checksums; }

  /// Return true if a string table subsection has been set.
  ///
  /// \returns True if a string table subsection has been set.
  bool hasStrings() const { return Strings != nullptr; }
  /// Return true if a file checksums subsection has been set.
  ///
  /// \returns True if a file checksums subsection has been set.
  bool hasChecksums() const { return Checksums != nullptr; }

private:
  LLVM_ABI void initializeStrings(const DebugSubsectionRecord &SR);
  LLVM_ABI void initializeChecksums(const DebugSubsectionRecord &FCR);

  std::shared_ptr<DebugStringTableSubsectionRef> OwnedStrings;
  std::shared_ptr<DebugChecksumsSubsectionRef> OwnedChecksums;

  const DebugStringTableSubsectionRef *Strings = nullptr;
  const DebugChecksumsSubsectionRef *Checksums = nullptr;
};

/// Owns shared string-table and checksums subsections used when building
/// CodeView debug info.
class StringsAndChecksums {
public:
  /// Shared ownership of a writable string table subsection.
  using StringsPtr = std::shared_ptr<DebugStringTableSubsection>;
  /// Shared ownership of a writable file checksums subsection.
  using ChecksumsPtr = std::shared_ptr<DebugChecksumsSubsection>;

  /// Construct an empty holder with no string table or checksums subsections.
  StringsAndChecksums() = default;

  /// Set the owned string table subsection to \p SP.
  ///
  /// \param SP Shared pointer to the string table subsection.
  void setStrings(const StringsPtr &SP) { Strings = SP; }
  /// Set the owned file checksums subsection to \p CP.
  ///
  /// \param CP Shared pointer to the file checksums subsection.
  void setChecksums(const ChecksumsPtr &CP) { Checksums = CP; }

  /// Return the shared pointer to the owned string table subsection.
  ///
  /// \returns The shared pointer to the owned string table subsection.
  const StringsPtr &strings() const { return Strings; }
  /// Return the shared pointer to the owned file checksums subsection.
  ///
  /// \returns The shared pointer to the owned file checksums subsection.
  const ChecksumsPtr &checksums() const { return Checksums; }

  /// Return true if a string table subsection has been set.
  ///
  /// \returns True if a string table subsection has been set.
  bool hasStrings() const { return Strings != nullptr; }
  /// Return true if a file checksums subsection has been set.
  ///
  /// \returns True if a file checksums subsection has been set.
  bool hasChecksums() const { return Checksums != nullptr; }

private:
  StringsPtr Strings;
  ChecksumsPtr Checksums;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_STRINGSANDCHECKSUMS_H
