//===- Archive.h - ar archive file format -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the ar archive file format class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ARCHIVE_H
#define LLVM_OBJECT_ARCHIVE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/fallible_iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
namespace object {

/// Magic string for a standard Unix ar archive.
const char ArchiveMagic[] = "!<arch>\n";
/// Magic string for a thin ar archive.
const char ThinArchiveMagic[] = "!<thin>\n";
/// Magic string for an AIX big archive.
const char BigArchiveMagic[] = "<bigaf>\n";
/// Magic string for a z/OS archive (EBCDIC encoding of the Unix ar magic).
const char ZOSArchiveMagic[] =
    "\x5A\x4C\x81\x99\x83\x88\x6E\x15"; // "!<arch>\n" in EBCDIC

class Archive;

/// Abstract base for format-specific archive member headers.
class AbstractArchiveMemberHeader {
protected:
  /// Construct a member header belonging to \p Parent.
  /// @param Parent Archive that owns this member header.
  AbstractArchiveMemberHeader(const Archive *Parent) : Parent(Parent){};

public:
  friend class Archive;
  /// Clone this header into a newly allocated instance.
  /// @return A newly allocated copy of this header.
  virtual std::unique_ptr<AbstractArchiveMemberHeader> clone() const = 0;
  /// Virtual destructor for polymorphic archive member headers.
  virtual ~AbstractArchiveMemberHeader() = default;

  /// Get the name without looking up long names.
  /// @return The raw member name, or an error on failure.
  virtual Expected<StringRef> getRawName() const = 0;
  /// Return the raw access-mode field from the member header.
  /// @return The raw access-mode field as a string.
  virtual StringRef getRawAccessMode() const = 0;
  /// Return the raw last-modified timestamp field from the member header.
  /// @return The raw last-modified field as a string.
  virtual StringRef getRawLastModified() const = 0;
  /// Return the raw user-id field from the member header.
  /// @return The raw user-id field as a string.
  virtual StringRef getRawUID() const = 0;
  /// Return the raw group-id field from the member header.
  /// @return The raw group-id field as a string.
  virtual StringRef getRawGID() const = 0;

  /// Get the name looking up long names.
  /// @param Size Size in bytes of the remaining archive data after this header.
  /// @return The resolved member name, or an error on failure.
  virtual Expected<StringRef> getName(uint64_t Size) const = 0;
  /// Return the member data size from the header.
  /// @return The member data size in bytes, or an error on failure.
  virtual Expected<uint64_t> getSize() const = 0;
  /// Return the byte offset of this member header within the archive.
  /// @return The byte offset of this member header within the archive.
  virtual uint64_t getOffset() const = 0;

  /// Get next file member location.
  /// @return A pointer to the next member header, or an error on failure.
  virtual Expected<const char *> getNextChildLoc() const = 0;
  /// Return whether this member is a thin (external) archive member.
  /// @return True if this member is thin, or an error on failure.
  virtual Expected<bool> isThin() const = 0;

  /// Return the member access permissions parsed from the header.
  /// @return The access permissions, or an error on failure.
  LLVM_ABI Expected<sys::fs::perms> getAccessMode() const;
  /// Return the member last-modified time parsed from the header.
  /// @return The last-modified time, or an error on failure.
  LLVM_ABI Expected<sys::TimePoint<std::chrono::seconds>>
  getLastModified() const;
  /// Return the member user id parsed from the header.
  /// @return The user id, or an error on failure.
  LLVM_ABI Expected<unsigned> getUID() const;
  /// Return the member group id parsed from the header.
  /// @return The group id, or an error on failure.
  LLVM_ABI Expected<unsigned> getGID() const;

  /// Returns the size in bytes of the format-defined member header of the
  /// concrete archive type.
  /// @return The size in bytes of the format-defined member header.
  virtual uint64_t getSizeOf() const = 0;

  /// Archive that owns this member header.
  const Archive *Parent;
};

/// Shared implementation for archive member headers backed by a fixed raw
/// header type \tparam T.
template <typename T>
class LLVM_ABI CommonArchiveMemberHeader : public AbstractArchiveMemberHeader {
public:
  /// Construct a header for \p Parent from the raw bytes at \p RawHeaderPtr.
  /// @param Parent Archive that owns this member.
  /// @param RawHeaderPtr Pointer to the format-specific raw header in the
  /// archive buffer.
  CommonArchiveMemberHeader(const Archive *Parent, const T *RawHeaderPtr)
      : AbstractArchiveMemberHeader(Parent), ArMemHdr(RawHeaderPtr){};
  /// Return the raw access-mode field from the member header.
  /// @return The raw access-mode field as a string.
  StringRef getRawAccessMode() const override;
  /// Return the raw last-modified timestamp field from the member header.
  /// @return The raw last-modified field as a string.
  StringRef getRawLastModified() const override;
  /// Return the raw user-id field from the member header.
  /// @return The raw user-id field as a string.
  StringRef getRawUID() const override;
  /// Return the raw group-id field from the member header.
  /// @return The raw group-id field as a string.
  StringRef getRawGID() const override;

  /// Return the byte offset of this member header within the archive.
  /// @return The byte offset of this member header within the archive.
  uint64_t getOffset() const override;
  /// Return the size in bytes of the raw header type \tparam T.
  /// @return The size in bytes of the raw header type \tparam T.
  uint64_t getSizeOf() const override { return sizeof(T); }

  /// Pointer to the format-specific raw member header in the archive buffer.
  T const *ArMemHdr;
};

/// Layout of a traditional Unix ar member header.
struct UnixArMemHdrType {
  char Name[16];         ///< Member name, space-padded or encoded long name.
  char LastModified[12]; ///< Last-modified time as a decimal string.
  char UID[6];           ///< User id as a decimal string.
  char GID[6];           ///< Group id as a decimal string.
  char AccessMode[8];    ///< Access mode as an octal string.
  char Size[10]; ///< Size of data, not including header or padding.
  char Terminator[2];    ///< Header terminator (backtick and newline).
};

/// Member header for GNU/BSD/COFF-style Unix ar archives.
class LLVM_ABI ArchiveMemberHeader
    : public CommonArchiveMemberHeader<UnixArMemHdrType> {
public:
  /// Construct a Unix ar member header from raw archive bytes.
  /// @param Parent Archive that owns this member.
  /// @param RawHeaderPtr Pointer to the start of the member header.
  /// @param Size Remaining archive size after \p RawHeaderPtr.
  /// @param Err Set on parse failure; may be null.
  ArchiveMemberHeader(const Archive *Parent, const char *RawHeaderPtr,
                      uint64_t Size, Error *Err);

  /// Clone this header into a newly allocated ArchiveMemberHeader.
  /// @return A newly allocated copy of this header.
  std::unique_ptr<AbstractArchiveMemberHeader> clone() const override {
    return std::make_unique<ArchiveMemberHeader>(*this);
  }

  /// Get the name without looking up long names.
  /// @return The raw member name, or an error on failure.
  Expected<StringRef> getRawName() const override;

  /// Get the name looking up long names.
  /// @param Size Remaining archive size used when resolving long names.
  /// @return The resolved member name, or an error on failure.
  Expected<StringRef> getName(uint64_t Size) const override;
  /// Return the member data size from the header.
  /// @return The member data size in bytes, or an error on failure.
  Expected<uint64_t> getSize() const override;
  /// Return a pointer to the next member header in the archive.
  /// @return A pointer to the next member header, or an error on failure.
  Expected<const char *> getNextChildLoc() const override;
  /// Return whether this member is a thin (external) archive member.
  /// @return True if this member is thin, or an error on failure.
  Expected<bool> isThin() const override;
};

/// Layout of an AIX big-archive member header.
struct BigArMemHdrType {
  char Size[20];       ///< File member size in decimal.
  char NextOffset[20]; ///< Next member offset in decimal.
  char PrevOffset[20]; ///< Previous member offset in decimal.
  char LastModified[12]; ///< Last-modified time as a decimal string.
  char UID[12];          ///< User id as a decimal string.
  char GID[12];          ///< Group id as a decimal string.
  char AccessMode[12];   ///< Access mode as an octal string.
  char NameLen[4]; ///< File member name length in decimal.
  union {
    char Name[2]; ///< Start of member name.
    char Terminator[2]; ///< Header terminator when the name is empty.
  };
};

/// Member header for AIX big archives.
class LLVM_ABI BigArchiveMemberHeader
    : public CommonArchiveMemberHeader<BigArMemHdrType> {

public:
  /// Construct an AIX big-archive member header from raw archive bytes.
  /// @param Parent Archive that owns this member.
  /// @param RawHeaderPtr Pointer to the start of the member header.
  /// @param Size Remaining archive size after \p RawHeaderPtr.
  /// @param Err Set on parse failure; may be null.
  BigArchiveMemberHeader(Archive const *Parent, const char *RawHeaderPtr,
                         uint64_t Size, Error *Err);
  /// Clone this header into a newly allocated BigArchiveMemberHeader.
  /// @return A newly allocated copy of this header.
  std::unique_ptr<AbstractArchiveMemberHeader> clone() const override {
    return std::make_unique<BigArchiveMemberHeader>(*this);
  }

  /// Get the name without looking up long names.
  /// @return The raw member name, or an error on failure.
  Expected<StringRef> getRawName() const override;
  /// Return the member name length field from the header.
  /// @return The member name length, or an error on failure.
  Expected<uint64_t> getRawNameSize() const;

  /// Get the name looking up long names.
  /// @param Size Remaining archive size used when resolving the name.
  /// @return The resolved member name, or an error on failure.
  Expected<StringRef> getName(uint64_t Size) const override;
  /// Return the member data size from the header.
  /// @return The member data size in bytes, or an error on failure.
  Expected<uint64_t> getSize() const override;
  /// Return a pointer to the next member header in the archive.
  /// @return A pointer to the next member header, or an error on failure.
  Expected<const char *> getNextChildLoc() const override;
  /// Return the next-member offset field from the header.
  /// @return The next-member offset, or an error on failure.
  Expected<uint64_t> getNextOffset() const;
  /// Return false; AIX big archives do not use thin members.
  /// @return Always false.
  Expected<bool> isThin() const override { return false; }
};

/// Member header for z/OS archives.
///
/// The fixed part of the member header (in EBCDIC) is:
/// struct ar_hdr {
///   char ar_name[16]; /* space-padded member name */
///   char ar_date[12]; /* date (decimal) */
///   char ar_uid[6];   /* user id (decimal) */
///   char ar_gid[6];   /* group id (decimal) */
///   char ar_mode[8];  /* access mode (octal) */
///   char ar_size[10]; /* length in bytes (decimal) */
///   char ar_fmag[2];  /* contains backtick (X'79'), followed by new line
///   (X'15') */
/// };
class LLVM_ABI ZOSArchiveMemberHeader : public ArchiveMemberHeader {
public:
  /// Construct a z/OS archive member header from raw archive bytes.
  /// @param Parent Archive that owns this member.
  /// @param RawHeaderPtr Pointer to the start of the member header.
  /// @param Size Remaining archive size after \p RawHeaderPtr.
  /// @param Err Set on parse failure; may be null.
  ZOSArchiveMemberHeader(Archive const *Parent, const char *RawHeaderPtr,
                         uint64_t Size, Error *Err);
  /// Clone this header into a newly allocated ZOSArchiveMemberHeader.
  /// @return A newly allocated copy of this header.
  std::unique_ptr<AbstractArchiveMemberHeader> clone() const override {
    return std::make_unique<ZOSArchiveMemberHeader>(*this);
  }

  /// Raw member name converted from EBCDIC to ASCII.
  std::string RawMemberName;
  /// Resolved member name converted from EBCDIC to ASCII.
  std::string MemberName;
  /// Last-modified field converted from EBCDIC to ASCII.
  std::string LastModified;
  /// User-id field converted from EBCDIC to ASCII.
  std::string UID;
  /// Group-id field converted from EBCDIC to ASCII.
  std::string GID;
  /// Access-mode field converted from EBCDIC to ASCII.
  std::string AccessMode;

  /// Decode EBCDIC header fields into the ASCII string members.
  /// @param Err Set on decode failure; may be null.
  /// @param Size Remaining archive size after the raw header.
  void setMemberHeaderStrings(Error *Err, uint64_t Size);

  /// Get the name without looking up long names.
  /// @return The raw member name, or an error on failure.
  Expected<StringRef> getRawName() const override;
  /// Get the name looking up long names.
  /// @param Size Remaining archive size used when resolving the name.
  /// @return The resolved member name, or an error on failure.
  Expected<StringRef> getName(uint64_t Size) const override;
  /// Return the raw access-mode field (ASCII-converted).
  /// @return The raw access-mode field as an ASCII string.
  StringRef getRawAccessMode() const override;
  /// Return the raw last-modified field (ASCII-converted).
  /// @return The raw last-modified field as an ASCII string.
  StringRef getRawLastModified() const override;
  /// Return the raw user-id field (ASCII-converted).
  /// @return The raw user-id field as an ASCII string.
  StringRef getRawUID() const override;
  /// Return the raw group-id field (ASCII-converted).
  /// @return The raw group-id field as an ASCII string.
  StringRef getRawGID() const override;
  /// Return the member data size from the header.
  /// @return The member data size in bytes, or an error on failure.
  Expected<uint64_t> getSize() const override;
  /// Return false; z/OS archives do not use thin members.
  /// @return Always false.
  Expected<bool> isThin() const override { return false; }
};

/// In-memory representation of an ar archive (Unix, thin, AIX big, or z/OS).
class LLVM_ABI Archive : public Binary {
  virtual void anchor();

public:
  /// A single member (child) of an archive.
  class Child {
    friend Archive;
    friend AbstractArchiveMemberHeader;

    const Archive *Parent;
    std::unique_ptr<AbstractArchiveMemberHeader> Header;
    /// Includes header but not padding byte.
    StringRef Data;
    /// Offset from Data to the start of the file.
    uint16_t StartOfFile;

    Expected<bool> isThinMember() const;

  public:
    /// Construct a child starting at \p Start within \p Parent.
    /// @param Parent Archive that contains this member.
    /// @param Start Pointer to the member header in the archive buffer.
    /// @param Err Set on parse failure; may be null.
    LLVM_ABI Child(const Archive *Parent, const char *Start, Error *Err);
    /// Construct a child from already-parsed member data.
    /// @param Parent Archive that contains this member.
    /// @param Data Slice covering the member header and contents.
    /// @param StartOfFile Offset from \p Data to the member file bytes.
    LLVM_ABI Child(const Archive *Parent, StringRef Data, uint16_t StartOfFile);

    /// Copy-construct a child, cloning the member header if present.
    /// @param C Child to copy.
    Child(const Child &C)
        : Parent(C.Parent), Data(C.Data), StartOfFile(C.StartOfFile) {
      if (C.Header)
        Header = C.Header->clone();
    }

    /// Move-construct a child, taking ownership of the header.
    /// @param C Child to move from.
    Child(Child &&C) {
      Parent = std::move(C.Parent);
      Header = std::move(C.Header);
      Data = C.Data;
      StartOfFile = C.StartOfFile;
    }

    /// Move-assign from \p C, transferring ownership of the header.
    /// @param C Child to move from.
    /// @return A reference to this child.
    Child &operator=(Child &&C) noexcept {
      if (&C == this)
        return *this;

      Parent = std::move(C.Parent);
      Header = std::move(C.Header);
      Data = C.Data;
      StartOfFile = C.StartOfFile;

      return *this;
    }

    /// Copy-assign from \p C, cloning the member header if present.
    /// @param C Child to copy.
    /// @return A reference to this child.
    Child &operator=(const Child &C) {
      if (&C == this)
        return *this;

      Parent = C.Parent;
      if (C.Header)
        Header = C.Header->clone();
      Data = C.Data;
      StartOfFile = C.StartOfFile;

      return *this;
    }

    /// Compare two children by the start of their data slices.
    /// @param other Child to compare against.
    /// @return True if both children refer to the same data start.
    bool operator==(const Child &other) const {
      assert(!Parent || !other.Parent || Parent == other.Parent);
      return Data.begin() == other.Data.begin();
    }

    /// Return the archive that contains this member.
    /// @return The parent archive, or null if none.
    const Archive *getParent() const { return Parent; }
    /// Return the next member in the archive, or an error.
    /// @return The next child, or an error on failure.
    LLVM_ABI Expected<Child> getNext() const;

    /// Return the member name after resolving long-name encodings.
    /// @return The resolved member name, or an error on failure.
    LLVM_ABI Expected<StringRef> getName() const;
    /// Return the full path for a thin member, or the member name otherwise.
    /// @return The full member path or name, or an error on failure.
    LLVM_ABI Expected<std::string> getFullName() const;
    /// Return the raw member name without resolving long names.
    /// @return The raw member name, or an error on failure.
    Expected<StringRef> getRawName() const { return Header->getRawName(); }

    /// Return the member last-modified time.
    /// @return The last-modified time, or an error on failure.
    Expected<sys::TimePoint<std::chrono::seconds>> getLastModified() const {
      return Header->getLastModified();
    }

    /// Return the raw last-modified field from the member header.
    /// @return The raw last-modified field as a string.
    StringRef getRawLastModified() const {
      return Header->getRawLastModified();
    }

    /// Return the member user id.
    /// @return The user id, or an error on failure.
    Expected<unsigned> getUID() const { return Header->getUID(); }
    /// Return the member group id.
    /// @return The group id, or an error on failure.
    Expected<unsigned> getGID() const { return Header->getGID(); }

    /// Return the member access permissions.
    /// @return The access permissions, or an error on failure.
    Expected<sys::fs::perms> getAccessMode() const {
      return Header->getAccessMode();
    }

    /// Return the size of the archive member without the header or padding.
    /// @return The member content size in bytes, or an error on failure.
    LLVM_ABI Expected<uint64_t> getSize() const;
    /// Return the size recorded in the archive header for this member.
    /// @return The size from the header, or an error on failure.
    LLVM_ABI Expected<uint64_t> getRawSize() const;

    /// Return a StringRef over the member file contents.
    /// @return The member file contents, or an error on failure.
    LLVM_ABI Expected<StringRef> getBuffer() const;
    /// Return the byte offset of this member within the archive.
    /// @return The byte offset of this member within the archive.
    LLVM_ABI uint64_t getChildOffset() const;
    /// Return the byte offset of this member's file data within the archive.
    /// @return The byte offset of this member's file data.
    uint64_t getDataOffset() const { return getChildOffset() + StartOfFile; }

    /// Return a MemoryBufferRef over the member file contents.
    /// @return A MemoryBufferRef over the member contents, or an error.
    LLVM_ABI Expected<MemoryBufferRef> getMemoryBufferRef() const;

    /// Parse this member as a Binary, optionally using \p Context for IR.
    /// @param Context Optional LLVM IR context used when the member is bitcode.
    /// @return The parsed Binary, or an error on failure.
    LLVM_ABI Expected<std::unique_ptr<Binary>>
    getAsBinary(LLVMContext *Context = nullptr) const;
  };

  /// Fallible iterator over archive children.
  class ChildFallibleIterator {
    Child C;

  public:
    /// Construct an end/sentinel child iterator.
    ChildFallibleIterator() : C(Child(nullptr, nullptr, nullptr)) {}
    /// Construct an iterator positioned at \p C.
    /// @param C Child to wrap.
    ChildFallibleIterator(const Child &C) : C(C) {}

    /// Return a pointer to the current child.
    /// @return A pointer to the current child.
    const Child *operator->() const { return &C; }
    /// Return a reference to the current child.
    /// @return A reference to the current child.
    const Child &operator*() const { return C; }

    /// Compare iterators by their underlying children.
    /// @param other Iterator to compare against.
    /// @return True if both iterators refer to the same child.
    bool operator==(const ChildFallibleIterator &other) const {
      // Ignore errors here: If an error occurred during increment then getNext
      // will have been set to child_end(), and the following comparison should
      // do the right thing.
      return C == other.C;
    }

    /// Return whether this iterator differs from \p other.
    /// @param other Iterator to compare against.
    /// @return True if the iterators refer to different children.
    bool operator!=(const ChildFallibleIterator &other) const {
      return !(*this == other);
    }

    /// Advance to the next child, returning any error from getNext().
    /// @return Success, or an error from advancing to the next child.
    Error inc() {
      auto NextChild = C.getNext();
      if (!NextChild)
        return NextChild.takeError();
      C = std::move(*NextChild);
      return Error::success();
    }
  };

  /// Fallible iterator type over archive Child members.
  using child_iterator = fallible_iterator<ChildFallibleIterator>;

  /// A symbol table entry referring to an archive member.
  class Symbol {
    const Archive *Parent;
    uint32_t SymbolIndex;
    uint32_t StringIndex; // Extra index to the string.

  public:
    /// Construct a symbol table entry for \p p.
    /// @param p Archive that owns the symbol table.
    /// @param symi Index of this symbol in the symbol table.
    /// @param stri Index into the string table for the symbol name.
    Symbol(const Archive *p, uint32_t symi, uint32_t stri)
        : Parent(p), SymbolIndex(symi), StringIndex(stri) {}

    /// Compare symbols by parent archive and symbol index.
    /// @param other Symbol to compare against.
    /// @return True if both symbols have the same parent and index.
    bool operator==(const Symbol &other) const {
      return (Parent == other.Parent) && (SymbolIndex == other.SymbolIndex);
    }

    /// Return the symbol name.
    /// @return The symbol name.
    LLVM_ABI StringRef getName() const;
    /// Return the archive member that defines this symbol.
    /// @return The defining archive member, or an error on failure.
    LLVM_ABI Expected<Child> getMember() const;
    /// Return the next symbol in the symbol table.
    /// @return The next symbol table entry.
    LLVM_ABI Symbol getNext() const;
    /// Return whether this entry comes from the EC (ARM64EC) symbol table.
    /// @return True if this is an EC symbol table entry.
    LLVM_ABI bool isECSymbol() const;

    /// Archive attribute bit masks for K_ZOS archive symbol table entries.
    static constexpr uint32_t ZOSAttrWSA = 0x1;
    /// z/OS symbol attribute bit for XPLink calling convention.
    static constexpr uint32_t ZOSAttrXPLink = 0x2;
    /// z/OS symbol attribute bit for 64-bit objects.
    static constexpr uint32_t ZOSAttr64Bit = 0x4;
    /// Mask of all recognized z/OS symbol attribute bits.
    static constexpr uint32_t ZOSKnownAttrMask =
        ZOSAttrWSA | ZOSAttrXPLink | ZOSAttr64Bit;

    /// Return the z/OS attribute word for this symbol table entry.
    ///
    /// For K_ZOS archives, returns the 32-bit attribute word stored alongside
    /// the symbol table entry. The low bits are described by the ZOSAttr*
    /// constants above. Returns 0 for non-z/OS archives.
    /// @return The z/OS attribute word, or 0 for non-z/OS archives.
    LLVM_ABI uint32_t getZOSAttributes() const;
  };

  /// Iterator over archive symbol table entries.
  class symbol_iterator {
    Symbol symbol;

  public:
    /// Construct an iterator positioned at \p s.
    /// @param s Symbol to wrap.
    symbol_iterator(const Symbol &s) : symbol(s) {}

    /// Return a pointer to the current symbol.
    /// @return A pointer to the current symbol.
    const Symbol *operator->() const { return &symbol; }
    /// Return a reference to the current symbol.
    /// @return A reference to the current symbol.
    const Symbol &operator*() const { return symbol; }

    /// Compare iterators by their underlying symbols.
    /// @param other Iterator to compare against.
    /// @return True if both iterators refer to the same symbol.
    bool operator==(const symbol_iterator &other) const {
      return symbol == other.symbol;
    }

    /// Return whether this iterator differs from \p other.
    /// @param other Iterator to compare against.
    /// @return True if the iterators refer to different symbols.
    bool operator!=(const symbol_iterator &other) const {
      return !(*this == other);
    }

    /// Advance to the next symbol (preincrement).
    /// @return A reference to this iterator after advancing.
    symbol_iterator &operator++() { // Preincrement
      symbol = symbol.getNext();
      return *this;
    }
  };

  /// Construct an Archive from \p Source, reporting parse errors via \p Err.
  /// @param Source Buffer containing the archive file.
  /// @param Err Set if the archive cannot be parsed.
  Archive(MemoryBufferRef Source, Error &Err);
  /// Create an Archive from \p Source, returning a unique_ptr or an error.
  /// @param Source Buffer containing the archive file.
  /// @return A unique_ptr to the Archive, or an error on failure.
  static Expected<std::unique_ptr<Archive>> create(MemoryBufferRef Source);

  /// Deleted copy constructor; archives are non-copyable.
  /// @param Other Unused; archives cannot be copied.
  Archive(Archive const &Other) = delete;
  /// Deleted copy assignment; archives are non-copyable.
  /// @param Other Unused; archives cannot be copy-assigned.
  Archive &operator=(Archive const &Other) = delete;

  /// Size field is 10 decimal digits long
  static const uint64_t MaxMemberSize = 9999999999;

  /// Archive format kinds recognized by this class.
  enum Kind {
    K_GNU,      ///< GNU ar archive.
    K_GNU64,    ///< GNU ar archive with 64-bit symbol table.
    K_BSD,      ///< BSD ar archive.
    K_DARWIN,   ///< Darwin/Mach-O ar archive.
    K_DARWIN64, ///< Darwin ar archive with 64-bit symbol table.
    K_COFF,     ///< COFF/Windows import library archive.
    K_AIXBIG,   ///< AIX big archive.
    K_ZOS       ///< z/OS archive.
  };

  /// Return the archive format kind.
  /// @return The archive format kind.
  Kind kind() const { return (Kind)Format; }
  /// Return whether this is a thin archive.
  /// @return True if this is a thin archive.
  bool isThin() const { return IsThin; }
  /// Return the default archive kind for the host.
  /// @return The default archive kind for the host.
  static object::Archive::Kind getDefaultKind();
  /// Return the default archive kind for target triple \p T.
  /// @param T Target triple used to select the archive format.
  /// @return The default archive kind for \p T.
  static object::Archive::Kind getDefaultKindForTriple(const Triple &T);

  /// Return an iterator to the first archive child.
  /// @param Err Set if iteration cannot start.
  /// @param SkipInternal If true, skip symbol/string table members.
  /// @return An iterator to the first archive child.
  child_iterator child_begin(Error &Err, bool SkipInternal = true) const;
  /// Return the end iterator for archive children.
  /// @return The end iterator for archive children.
  child_iterator child_end() const;
  /// Return a range over the archive children.
  /// @param Err Set if iteration cannot start.
  /// @param SkipInternal If true, skip symbol/string table members.
  /// @return A range over the archive children.
  iterator_range<child_iterator> children(Error &Err,
                                          bool SkipInternal = true) const {
    return make_range(child_begin(Err, SkipInternal), child_end());
  }

  /// Return an iterator to the first symbol table entry.
  /// @return An iterator to the first symbol table entry.
  symbol_iterator symbol_begin() const;
  /// Return the end iterator for symbol table entries.
  /// @return The end iterator for symbol table entries.
  symbol_iterator symbol_end() const;
  /// Return a range over the archive symbol table.
  /// @return A range over the archive symbol table.
  iterator_range<symbol_iterator> symbols() const {
    return make_range(symbol_begin(), symbol_end());
  }

  /// Return a range over the EC (ARM64EC) symbol table, if present.
  /// @return A range over the EC symbol table, or an error on failure.
  Expected<iterator_range<symbol_iterator>> ec_symbols() const;

  /// Return true if \p v is an Archive.
  /// @param v Binary to test.
  /// @return True if \p v is an Archive.
  static bool classof(Binary const *v) { return v->isArchive(); }

  /// Find the archive member that defines symbol \p name, if any.
  /// @param name Symbol name to look up in the archive symbol table.
  /// @return The defining child if found, nullopt if not, or an error.
  Expected<std::optional<Child>> findSym(StringRef name) const;

  /// Return whether the archive contains no regular members.
  /// @return True if the archive contains no regular members.
  virtual bool isEmpty() const;
  /// Return whether this archive has a symbol table.
  /// @return True if this archive has a symbol table.
  bool hasSymbolTable() const;
  /// Return the raw archive symbol table bytes.
  /// @return The raw archive symbol table bytes.
  StringRef getSymbolTable() const { return SymbolTable; }
  /// Return the raw archive string table bytes.
  /// @return The raw archive string table bytes.
  StringRef getStringTable() const { return StringTable; }
  /// Return the number of entries in the archive symbol table.
  /// @return The number of entries in the archive symbol table.
  uint32_t getNumberOfSymbols() const;
  /// Return the number of entries in the EC symbol table.
  /// @return The number of entries in the EC symbol table.
  uint32_t getNumberOfECSymbols() const;
  /// Return the byte offset of the first child member.
  /// @return The byte offset of the first child member.
  virtual uint64_t getFirstChildOffset() const { return getArchiveMagicLen(); }

  /// Take ownership of MemoryBuffers opened for thin archive members.
  /// @return The transferred thin-member MemoryBuffers.
  std::vector<std::unique_ptr<MemoryBuffer>> takeThinBuffers() {
    return std::move(ThinBuffers);
  }

  /// Create a format-specific member header from raw archive bytes.
  /// @param RawHeaderPtr Pointer to the start of the member header.
  /// @param Size Remaining archive size after \p RawHeaderPtr.
  /// @param Err Set on parse failure; may be null.
  /// @return A newly created format-specific member header.
  std::unique_ptr<AbstractArchiveMemberHeader>
  createArchiveMemberHeader(const char *RawHeaderPtr, uint64_t Size,
                            Error *Err) const;

protected:
  /// Return the length of this archive's magic string.
  /// @return The length of this archive's magic string.
  uint64_t getArchiveMagicLen() const;
  /// Record \p C as the first regular (non-internal) archive member.
  /// @param C First regular child member.
  void setFirstRegular(const Child &C);

  /// Raw archive symbol table bytes.
  StringRef SymbolTable;
  /// Raw EC (ARM64EC) symbol table bytes.
  StringRef ECSymbolTable;
  /// Raw archive string table bytes.
  StringRef StringTable;

private:
  StringRef FirstRegularData;
  uint16_t FirstRegularStartOfFile = -1;

  unsigned Format : 3;
  unsigned IsThin : 1;
  mutable std::vector<std::unique_ptr<MemoryBuffer>> ThinBuffers;
};

/// AIX big archive (bigaf) specialization of Archive.
class BigArchive : public Archive {
public:
  /// Fixed-length header at the start of an AIX big archive.
  struct FixLenHdr {
    char Magic[sizeof(BigArchiveMagic) - 1]; ///< Big archive magic string.
    char MemOffset[20];                      ///< Offset to member table.
    char GlobSymOffset[20];                  ///< Offset to global symbol table.
    char
        GlobSym64Offset[20]; ///< Offset global symbol table for 64-bit objects.
    char FirstChildOffset[20]; ///< Offset to first archive member.
    char LastChildOffset[20];  ///< Offset to last archive member.
    char FreeOffset[20];       ///< Offset to first mem on free list.
  };

  /// Pointer to the fixed-length header in the archive buffer.
  const FixLenHdr *ArFixLenHdr;
  /// Byte offset of the first archive member.
  uint64_t FirstChildOffset = 0;
  /// Byte offset of the last archive member.
  uint64_t LastChildOffset = 0;
  /// Buffer holding the merged 32/64-bit global symbol table, if built.
  std::string MergedGlobalSymtabBuf;
  /// True if a 32-bit global symbol table is present.
  bool Has32BitGlobalSymtab = false;
  /// True if a 64-bit global symbol table is present.
  bool Has64BitGlobalSymtab = false;

public:
  /// Construct a BigArchive from \p Source, reporting parse errors via \p Err.
  /// @param Source Buffer containing the big archive file.
  /// @param Err Set if the archive cannot be parsed.
  LLVM_ABI BigArchive(MemoryBufferRef Source, Error &Err);
  /// Return the byte offset of the first archive member.
  /// @return The byte offset of the first archive member.
  uint64_t getFirstChildOffset() const override { return FirstChildOffset; }
  /// Return the byte offset of the last archive member.
  /// @return The byte offset of the last archive member.
  uint64_t getLastChildOffset() const { return LastChildOffset; }
  /// Return whether the archive has no members (first child offset is zero).
  /// @return True if the first child offset is zero.
  bool isEmpty() const override { return getFirstChildOffset() == 0; }

  /// Return whether a 32-bit global symbol table is present.
  /// @return True if a 32-bit global symbol table is present.
  bool has32BitGlobalSymtab() { return Has32BitGlobalSymtab; }
  /// Return whether a 64-bit global symbol table is present.
  /// @return True if a 64-bit global symbol table is present.
  bool has64BitGlobalSymtab() { return Has64BitGlobalSymtab; }
};

/// z/OS archive specialization of Archive.
class ZOSArchive : public Archive {
public:
  /// Fixed-length header at the start of a z/OS archive.
  struct FixLenHdr {
    char Magic[sizeof(ZOSArchiveMagic) - 1]; ///< ZOS archive magic string.
  };

  /// Construct a ZOSArchive from \p Source, reporting parse errors via \p Err.
  /// @param Source Buffer containing the z/OS archive file.
  /// @param Err Set if the archive cannot be parsed.
  LLVM_ABI ZOSArchive(MemoryBufferRef Source, Error &Err);

private:
  std::string SymbolTableBuf; // __.SYMDEF strings converted to ASCII.
};
} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_ARCHIVE_H
