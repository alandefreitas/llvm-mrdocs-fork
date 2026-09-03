//===-- TapiUniversal.h - Text-based Dynamic Library Stub -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the TapiUniversal interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_TAPIUNIVERSAL_H
#define LLVM_OBJECT_TAPIUNIVERSAL_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/InterfaceFile.h"

namespace llvm {
namespace object {

class TapiFile;

/// A text-based dynamic library stub (TAPI/TBD) that may contain multiple
/// architectures or inlined libraries.
class LLVM_ABI TapiUniversal : public Binary {
public:
  /// A single architecture/library entry within a TAPI universal file.
  class ObjectForArch {
    const TapiUniversal *Parent;
    int Index;

  public:
    /// Construct a view of the library entry at \p Index in \p Parent.
    ///
    /// \param Parent TAPI universal binary that owns this entry.
    /// \param Index Zero-based index of the library entry.
    ObjectForArch(const TapiUniversal *Parent, int Index)
        : Parent(Parent), Index(Index) {}

    /// Return the next library entry in the TAPI universal file.
    ///
    /// \return The next library entry.
    ObjectForArch getNext() const { return ObjectForArch(Parent, Index + 1); }

    /// Compare two library entries for equality by parent and index.
    ///
    /// \param Other Library entry to compare against.
    /// \return True if both entries have the same parent and index.
    bool operator==(const ObjectForArch &Other) const {
      return (Parent == Other.Parent) && (Index == Other.Index);
    }

    /// Return the Mach-O CPU type of this library entry.
    ///
    /// \return The Mach-O CPU type of this library entry.
    uint32_t getCPUType() const {
      auto Result =
          MachO::getCPUTypeFromArchitecture(Parent->Libraries[Index].Arch);
      return Result.first;
    }

    /// Return the Mach-O CPU subtype of this library entry.
    ///
    /// \return The Mach-O CPU subtype of this library entry.
    uint32_t getCPUSubType() const {
      auto Result =
          MachO::getCPUTypeFromArchitecture(Parent->Libraries[Index].Arch);
      return Result.second;
    }

    /// Return the architecture flag name for this library entry.
    ///
    /// \return The architecture flag name for this library entry.
    StringRef getArchFlagName() const {
      return MachO::getArchitectureName(Parent->Libraries[Index].Arch);
    }

    /// Return the install name of this library entry.
    ///
    /// \return The install name of this library entry.
    std::string getInstallName() const {
      return std::string(Parent->Libraries[Index].InstallName);
    }

    /// Return true if this entry is the top-level library of the TBD file.
    ///
    /// \return True if this entry is the top-level library of the TBD file.
    bool isTopLevelLib() const {
      return Parent->ParsedFile->getInstallName() == getInstallName();
    }

    /// Create a TapiFile object file for this library entry.
    ///
    /// \return The TapiFile for this library entry, or an error on failure.
    LLVM_ABI Expected<std::unique_ptr<TapiFile>> getAsObjectFile() const;
  };

  /// Iterator over library entries in a TAPI universal file.
  class object_iterator {
    ObjectForArch Obj;

  public:
    /// Construct an iterator positioned at \p Obj.
    ///
    /// \param Obj Library entry to wrap.
    object_iterator(const ObjectForArch &Obj) : Obj(Obj) {}
    /// Access the current library entry.
    ///
    /// \return Pointer to the current library entry.
    const ObjectForArch *operator->() const { return &Obj; }
    /// Dereference to the current library entry.
    ///
    /// \return Reference to the current library entry.
    const ObjectForArch &operator*() const { return Obj; }

    /// Compare two iterators for equality.
    ///
    /// \param Other Iterator to compare against.
    /// \return True if both iterators refer to the same library entry.
    bool operator==(const object_iterator &Other) const {
      return Obj == Other.Obj;
    }
    /// Compare two iterators for inequality.
    ///
    /// \param Other Iterator to compare against.
    /// \return True if the iterators refer to different library entries.
    bool operator!=(const object_iterator &Other) const {
      return !(*this == Other);
    }

    /// Advance to the next library entry (preincrement).
    ///
    /// \return Reference to this iterator after advancing.
    object_iterator &operator++() { // Preincrement
      Obj = Obj.getNext();
      return *this;
    }
  };

  /// Construct a TAPI universal binary from \p Source, reporting errors via
  /// \p Err.
  ///
  /// \param Source Memory buffer holding the TAPI/TBD text.
  /// \param SkipUnknownTriples If true, skip library entries with unknown
  ///        target triples instead of failing.
  /// \param Err Set on parse failure; left unmodified on success.
  TapiUniversal(MemoryBufferRef Source, bool SkipUnknownTriples, Error &Err);
  /// Create a TapiUniversal from a memory buffer.
  ///
  /// \param Source Memory buffer holding the TAPI/TBD text.
  /// \param SkipUnknownTriples If true, skip library entries with unknown
  ///        target triples instead of failing.
  /// \return A unique pointer to the created binary, or an error on failure.
  static Expected<std::unique_ptr<TapiUniversal>>
  create(MemoryBufferRef Source, bool SkipUnknownTriples = false);
  /// Destroy this TapiUniversal.
  ~TapiUniversal() override;

  /// Return an iterator to the first library entry.
  ///
  /// \return An iterator to the first library entry.
  object_iterator begin_objects() const { return ObjectForArch(this, 0); }
  /// Return an iterator past the last library entry.
  ///
  /// \return An iterator past the last library entry.
  object_iterator end_objects() const {
    return ObjectForArch(this, Libraries.size());
  }

  /// Return a range over all library entries in this binary.
  ///
  /// \return A range over all library entries in this binary.
  iterator_range<object_iterator> objects() const {
    return make_range(begin_objects(), end_objects());
  }

  /// Return the parsed Mach-O interface file backing this binary.
  ///
  /// \return The parsed Mach-O interface file backing this binary.
  const MachO::InterfaceFile &getInterfaceFile() { return *ParsedFile; }

  /// Return the number of library entries in this binary.
  ///
  /// \return The number of library entries in this binary.
  uint32_t getNumberOfObjects() const { return Libraries.size(); }

  /// Return true if \p v is a TapiUniversal.
  ///
  /// \param v Binary to test.
  /// \return True if \p v is a TapiUniversal.
  static bool classof(const Binary *v) { return v->isTapiUniversal(); }

private:
  /// Attributes of a library that is inlined into a single TBD file.
  struct Library {
    const StringRef InstallName;
    const MachO::Architecture Arch;
    const std::optional<size_t> DocumentIdx;
  };

  std::unique_ptr<MachO::InterfaceFile> ParsedFile;
  std::vector<Library> Libraries;
};

} // end namespace object.
} // end namespace llvm.

#endif // LLVM_OBJECT_TAPIUNIVERSAL_H
