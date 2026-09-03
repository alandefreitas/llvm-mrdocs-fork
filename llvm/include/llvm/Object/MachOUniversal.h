//===- MachOUniversal.h - Mach-O universal binaries -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares Mach-O fat/universal binaries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_MACHOUNIVERSAL_H
#define LLVM_OBJECT_MACHOUNIVERSAL_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
class StringRef;
class LLVMContext;

namespace object {
class Archive;
class IRObjectFile;

/// A Mach-O fat/universal binary containing architecture-specific objects.
class LLVM_ABI MachOUniversalBinary : public Binary {
  virtual void anchor();

  uint32_t Magic;
  uint32_t NumberOfObjects;
public:
  /// Maximum power-of-two section alignment allowed in a fat binary (2**15).
  static constexpr uint32_t MaxSectionAlignment = 15; /* 2**15 or 0x8000 */

  /// A single architecture slice within a Mach-O universal binary.
  class ObjectForArch {
    const MachOUniversalBinary *Parent;
    /// Index of object in the universal binary.
    uint32_t Index;
    /// Descriptor of the object.
    MachO::fat_arch Header;
    MachO::fat_arch_64 Header64;

  public:
    /// Construct a view of the architecture slice at \p Index in \p Parent.
    ///
    /// \param Parent Universal binary that owns this slice.
    /// \param Index Zero-based index of the slice in the fat header.
    LLVM_ABI ObjectForArch(const MachOUniversalBinary *Parent, uint32_t Index);

    /// Reset this slice to an empty/invalid state.
    void clear() {
      Parent = nullptr;
      Index = 0;
    }

    /// Compare two slices for equality by parent and index.
    ///
    /// \param Other Slice to compare against.
    /// \return True if both slices have the same parent and index.
    bool operator==(const ObjectForArch &Other) const {
      return (Parent == Other.Parent) && (Index == Other.Index);
    }

    /// Return the next architecture slice in the universal binary.
    ///
    /// \return The next architecture slice.
    ObjectForArch getNext() const { return ObjectForArch(Parent, Index + 1); }
    /// Return the Mach-O CPU type of this slice.
    ///
    /// \return The Mach-O CPU type of this slice.
    uint32_t getCPUType() const {
      if (Parent->getMagic() == MachO::FAT_MAGIC)
        return Header.cputype;
      else // Parent->getMagic() == MachO::FAT_MAGIC_64
        return Header64.cputype;
    }
    /// Return the Mach-O CPU subtype of this slice.
    ///
    /// \return The Mach-O CPU subtype of this slice.
    uint32_t getCPUSubType() const {
      if (Parent->getMagic() == MachO::FAT_MAGIC)
        return Header.cpusubtype;
      else // Parent->getMagic() == MachO::FAT_MAGIC_64
        return Header64.cpusubtype;
    }
    /// Return the file offset of this slice within the universal binary.
    ///
    /// \return The file offset of this slice within the universal binary.
    uint64_t getOffset() const {
      if (Parent->getMagic() == MachO::FAT_MAGIC)
        return Header.offset;
      else // Parent->getMagic() == MachO::FAT_MAGIC_64
        return Header64.offset;
    }
    /// Return the size in bytes of this slice.
    ///
    /// \return The size in bytes of this slice.
    uint64_t getSize() const {
      if (Parent->getMagic() == MachO::FAT_MAGIC)
        return Header.size;
      else // Parent->getMagic() == MachO::FAT_MAGIC_64
        return Header64.size;
    }
    /// Return the power-of-two alignment of this slice.
    ///
    /// \return The power-of-two alignment of this slice.
    uint32_t getAlign() const {
      if (Parent->getMagic() == MachO::FAT_MAGIC)
        return Header.align;
      else // Parent->getMagic() == MachO::FAT_MAGIC_64
        return Header64.align;
    }
    /// Return the reserved field from a 64-bit fat arch header, or 0.
    ///
    /// \return The reserved field from a 64-bit fat arch header, or 0.
    uint32_t getReserved() const {
      if (Parent->getMagic() == MachO::FAT_MAGIC)
        return 0;
      else // Parent->getMagic() == MachO::FAT_MAGIC_64
        return Header64.reserved;
    }
    /// Return the LLVM Triple for this slice's architecture.
    ///
    /// \return The LLVM Triple for this slice's architecture.
    Triple getTriple() const {
      return MachOObjectFile::getArchTriple(getCPUType(), getCPUSubType());
    }
    /// Return the architecture flag name for this slice, or an empty string.
    ///
    /// \return The architecture flag name for this slice, or an empty string.
    std::string getArchFlagName() const {
      const char *McpuDefault, *ArchFlag;
      MachOObjectFile::getArchTriple(getCPUType(), getCPUSubType(),
                                     &McpuDefault, &ArchFlag);
      return ArchFlag ? ArchFlag : std::string();
    }

    /// Parse this slice as a Mach-O object file.
    ///
    /// \return The parsed Mach-O object file, or an error on failure.
    LLVM_ABI Expected<std::unique_ptr<MachOObjectFile>> getAsObjectFile() const;
    /// Parse this slice as an LLVM IR object file.
    ///
    /// \param Ctx LLVM context used when materializing the IR module.
    /// \return The parsed LLVM IR object file, or an error on failure.
    LLVM_ABI Expected<std::unique_ptr<IRObjectFile>>
    getAsIRObject(LLVMContext &Ctx) const;

    /// Parse this slice as a static archive.
    ///
    /// \return The parsed static archive, or an error on failure.
    LLVM_ABI Expected<std::unique_ptr<Archive>> getAsArchive() const;
  };

  /// Iterator over architecture slices in a Mach-O universal binary.
  class object_iterator {
    ObjectForArch Obj;
  public:
    /// Construct an iterator positioned at \p Obj.
    ///
    /// \param Obj Architecture slice to wrap.
    object_iterator(const ObjectForArch &Obj) : Obj(Obj) {}
    /// Access the current architecture slice.
    ///
    /// \return Pointer to the current architecture slice.
    const ObjectForArch *operator->() const { return &Obj; }
    /// Dereference to the current architecture slice.
    ///
    /// \return Reference to the current architecture slice.
    const ObjectForArch &operator*() const { return Obj; }

    /// Compare two iterators for equality.
    ///
    /// \param Other Iterator to compare against.
    /// \return True if both iterators refer to the same slice.
    bool operator==(const object_iterator &Other) const {
      return Obj == Other.Obj;
    }
    /// Compare two iterators for inequality.
    ///
    /// \param Other Iterator to compare against.
    /// \return True if the iterators refer to different slices.
    bool operator!=(const object_iterator &Other) const {
      return !(*this == Other);
    }

    /// Advance to the next architecture slice (preincrement).
    ///
    /// \return Reference to this iterator after advancing.
    object_iterator& operator++() {  // Preincrement
      Obj = Obj.getNext();
      return *this;
    }
  };

  /// Construct a Mach-O universal binary from \p Souce, reporting errors via \p Err.
  ///
  /// \param Souce Memory buffer holding the fat binary contents.
  /// \param Err Set on parse failure; left unmodified on success.
  MachOUniversalBinary(MemoryBufferRef Souce, Error &Err);
  /// Create a MachOUniversalBinary from a memory buffer.
  ///
  /// \param Source Memory buffer holding the fat binary contents.
  /// \return A unique pointer to the created binary, or an error on failure.
  static Expected<std::unique_ptr<MachOUniversalBinary>>
  create(MemoryBufferRef Source);

  /// Return an iterator to the first architecture slice.
  ///
  /// \return An iterator to the first architecture slice.
  object_iterator begin_objects() const {
    return ObjectForArch(this, 0);
  }
  /// Return an iterator past the last architecture slice.
  ///
  /// \return An iterator past the last architecture slice.
  object_iterator end_objects() const {
    return ObjectForArch(nullptr, 0);
  }

  /// Return a range over all architecture slices in this binary.
  ///
  /// \return A range over all architecture slices in this binary.
  iterator_range<object_iterator> objects() const {
    return make_range(begin_objects(), end_objects());
  }

  /// Return the Mach-O fat magic number (FAT_MAGIC or FAT_MAGIC_64).
  ///
  /// \return The Mach-O fat magic number (FAT_MAGIC or FAT_MAGIC_64).
  uint32_t getMagic() const { return Magic; }
  /// Return the number of architecture slices in this binary.
  ///
  /// \return The number of architecture slices in this binary.
  uint32_t getNumberOfObjects() const { return NumberOfObjects; }

  // Cast methods.
  /// Return true if \p V is a MachOUniversalBinary.
  ///
  /// \param V Binary to test.
  /// \return True if \p V is a MachOUniversalBinary.
  static bool classof(Binary const *V) {
    return V->isMachOUniversalBinary();
  }

  /// Look up the architecture slice named by \p ArchName.
  ///
  /// \param ArchName Architecture flag name to search for.
  /// \return The matching architecture slice, or an error if not found.
  Expected<ObjectForArch>
  getObjectForArch(StringRef ArchName) const;

  /// Return the Mach-O object file for the architecture named by \p ArchName.
  ///
  /// \param ArchName Architecture flag name to search for.
  /// \return The Mach-O object file for that architecture, or an error.
  Expected<std::unique_ptr<MachOObjectFile>>
  getMachOObjectForArch(StringRef ArchName) const;

  /// Return the LLVM IR object for the architecture named by \p ArchName.
  ///
  /// \param ArchName Architecture flag name to search for.
  /// \param Ctx LLVM context used when materializing the IR module.
  /// \return The LLVM IR object for that architecture, or an error.
  Expected<std::unique_ptr<IRObjectFile>>
  getIRObjectForArch(StringRef ArchName, LLVMContext &Ctx) const;

  /// Return the archive for the architecture named by \p ArchName.
  ///
  /// \param ArchName Architecture flag name to search for.
  /// \return The archive for that architecture, or an error.
  Expected<std::unique_ptr<Archive>>
  getArchiveForArch(StringRef ArchName) const;
};
}
}

#endif
