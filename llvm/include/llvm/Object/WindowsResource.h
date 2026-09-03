//===-- WindowsResource.h ---------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file declares the .res file class.  .res files are intermediate
// products of the typical resource-compilation process on Windows.  This
// process is as follows:
//
// .rc file(s) ---(rc.exe)---> .res file(s) ---(cvtres.exe)---> COFF file
//
// .rc files are human-readable scripts that list all resources a program uses.
//
// They are compiled into .res files, which are a list of the resources in
// binary form.
//
// Finally the data stored in the .res is compiled into a COFF file, where it
// is organized in a directory tree structure for optimized access by the
// program during runtime.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/ms648007(v=vs.85).aspx
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_WINDOWSRESOURCE_H
#define LLVM_OBJECT_WINDOWSRESOURCE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"

#include <map>

namespace llvm {

class raw_ostream;
class ScopedPrinter;

namespace object {

class WindowsResource;
class ResourceSectionRef;
struct coff_resource_dir_table;

/// Size in bytes of the leading magic header in a .res file.
const size_t WIN_RES_MAGIC_SIZE = 16;
/// Size in bytes of the null resource entry that follows the magic header.
const size_t WIN_RES_NULL_ENTRY_SIZE = 16;
/// Required alignment in bytes for resource headers in a .res file.
const uint32_t WIN_RES_HEADER_ALIGNMENT = 4;
/// Required alignment in bytes for resource data in a .res file.
const uint32_t WIN_RES_DATA_ALIGNMENT = 4;
/// Default MEMORYFLAGS value meaning the resource is pure and moveable.
const uint16_t WIN_RES_PURE_MOVEABLE = 0x0030;

/// Fixed-size prefix of a Windows .res resource header (data and header sizes).
struct WinResHeaderPrefix {
  /// Size in bytes of the resource data that follows the header.
  support::ulittle32_t DataSize;
  /// Size in bytes of the full resource header, including this prefix.
  support::ulittle32_t HeaderSize;
};

/// Integer type and name IDs for a .res entry when both are numeric.
///
/// Type and Name may each either be an integer ID or a string.  This struct is
/// only used in the case where they are both IDs.
struct WinResIDs {
  /// Marker (0xffff) indicating that TypeID is an integer ID, not a string.
  uint16_t TypeFlag;
  /// Integer resource type identifier.
  support::ulittle16_t TypeID;
  /// Marker (0xffff) indicating that NameID is an integer ID, not a string.
  uint16_t NameFlag;
  /// Integer resource name identifier.
  support::ulittle16_t NameID;

  /// Set the type to integer ID \p ID (with TypeFlag = 0xffff).
  ///
  /// \param ID Integer resource type identifier.
  void setType(uint16_t ID) {
    TypeFlag = 0xffff;
    TypeID = ID;
  }

  /// Set the name to integer ID \p ID (with NameFlag = 0xffff).
  ///
  /// \param ID Integer resource name identifier.
  void setName(uint16_t ID) {
    NameFlag = 0xffff;
    NameID = ID;
  }
};

/// Trailing fields of a Windows .res resource header after type and name.
struct WinResHeaderSuffix {
  /// Version of the resource data format.
  support::ulittle32_t DataVersion;
  /// MEMORYFLAGS for the resource (for example pure/moveable).
  support::ulittle16_t MemoryFlags;
  /// Language ID for this resource instance.
  support::ulittle16_t Language;
  /// Packed major/minor version of the resource.
  support::ulittle32_t Version;
  /// Additional characteristics bits for the resource.
  support::ulittle32_t Characteristics;
};

/// Error indicating that a .res file contains no resource entries.
class EmptyResError : public GenericBinaryError {
public:
  /// Construct an empty-.res error with message \p Msg and code \p ECOverride.
  ///
  /// \param Msg Human-readable description of the error.
  /// \param ECOverride Object-error code to associate with this error.
  EmptyResError(Twine Msg, object_error ECOverride)
      : GenericBinaryError(Msg, ECOverride) {}
};

/// Reference to a single resource entry while iterating a WindowsResource.
class ResourceEntryRef {
public:
  /// Advance to the next resource entry, setting \p End when none remain.
  ///
  /// \param End Set to true if iteration has reached the end of the file.
  /// \return Success, or an error if the next entry cannot be read.
  LLVM_ABI Error moveNext(bool &End);
  /// True if the resource type is identified by a string rather than an ID.
  ///
  /// \return True if the type is a string; false if it is an integer ID.
  bool checkTypeString() const { return IsStringType; }
  /// UTF-16 type string when checkTypeString() is true.
  ///
  /// \return The type name as a sequence of UTF-16 code units.
  ArrayRef<UTF16> getTypeString() const { return Type; }
  /// Integer type ID when checkTypeString() is false.
  ///
  /// \return The numeric resource type identifier.
  uint16_t getTypeID() const { return TypeID; }
  /// True if the resource name is identified by a string rather than an ID.
  ///
  /// \return True if the name is a string; false if it is an integer ID.
  bool checkNameString() const { return IsStringName; }
  /// UTF-16 name string when checkNameString() is true.
  ///
  /// \return The resource name as a sequence of UTF-16 code units.
  ArrayRef<UTF16> getNameString() const { return Name; }
  /// Integer name ID when checkNameString() is false.
  ///
  /// \return The numeric resource name identifier.
  uint16_t getNameID() const { return NameID; }
  /// Data-format version from the resource header suffix.
  ///
  /// \return The DataVersion field from the header suffix.
  uint16_t getDataVersion() const { return Suffix->DataVersion; }
  /// Language ID from the resource header suffix.
  ///
  /// \return The language identifier for this resource instance.
  uint16_t getLanguage() const { return Suffix->Language; }
  /// MEMORYFLAGS from the resource header suffix.
  ///
  /// \return The MEMORYFLAGS value for this resource.
  uint16_t getMemoryFlags() const { return Suffix->MemoryFlags; }
  /// Major version from the packed Version field of the header suffix.
  ///
  /// \return The high 16 bits of the Version field.
  uint16_t getMajorVersion() const { return Suffix->Version >> 16; }
  /// Minor version from the packed Version field of the header suffix.
  ///
  /// \return The low 16 bits of the Version field.
  uint16_t getMinorVersion() const { return Suffix->Version; }
  /// Characteristics bits from the resource header suffix.
  ///
  /// \return The Characteristics field from the header suffix.
  uint32_t getCharacteristics() const { return Suffix->Characteristics; }
  /// Raw bytes of the resource data payload.
  ///
  /// \return The resource data bytes for this entry.
  ArrayRef<uint8_t> getData() const { return Data; }

private:
  friend class WindowsResource;

  ResourceEntryRef(BinaryStreamRef Ref, const WindowsResource *Owner);
  Error loadNext();

  static Expected<ResourceEntryRef> create(BinaryStreamRef Ref,
                                           const WindowsResource *Owner);

  BinaryStreamReader Reader;
  const WindowsResource *Owner;
  bool IsStringType;
  ArrayRef<UTF16> Type;
  uint16_t TypeID;
  bool IsStringName;
  ArrayRef<UTF16> Name;
  uint16_t NameID;
  const WinResHeaderSuffix *Suffix = nullptr;
  ArrayRef<uint8_t> Data;
};

/// Binary representing a Windows .res resource object file.
class WindowsResource : public Binary {
public:
  /// First resource entry in this .res file, or an error if there are none.
  ///
  /// \return A reference to the first entry, or an error if the file is empty.
  LLVM_ABI Expected<ResourceEntryRef> getHeadEntry();

  /// True if \p V is a WindowsResource (.res) binary.
  ///
  /// \param V Binary to test.
  /// \return True if \p V is a Windows .res resource binary.
  static bool classof(const Binary *V) { return V->isWinRes(); }

  /// Create a WindowsResource from the contents of \p Source.
  ///
  /// \param Source Memory buffer containing a .res file.
  /// \return An owned WindowsResource, or an error if \p Source is invalid.
  LLVM_ABI static Expected<std::unique_ptr<WindowsResource>>
  createWindowsResource(MemoryBufferRef Source);

private:
  friend class ResourceEntryRef;

  WindowsResource(MemoryBufferRef Source);

  BinaryByteStream BBS;
};

/// Parses Windows resources into a directory tree suitable for COFF emission.
class WindowsResourceParser {
public:
  class TreeNode;
  /// Construct a parser; when \p MinGW is true, apply MinGW duplicate rules.
  ///
  /// \param MinGW If true, use MinGW-compatible handling of duplicate resources.
  LLVM_ABI WindowsResourceParser(bool MinGW = false);
  /// Parse resources from .res file \p WR, recording duplicates in \p Duplicates.
  ///
  /// \param WR Windows .res object to parse.
  /// \param Duplicates Receives human-readable descriptions of duplicate entries.
  /// \return Success, or an error if parsing fails.
  LLVM_ABI Error parse(WindowsResource *WR,
                       std::vector<std::string> &Duplicates);
  /// Parse resources from COFF section \p RSR named \p Filename.
  ///
  /// \param RSR Resource section reference to parse.
  /// \param Filename Source file name used in diagnostics and duplicate reports.
  /// \param Duplicates Receives human-readable descriptions of duplicate entries.
  /// \return Success, or an error if parsing fails.
  LLVM_ABI Error parse(ResourceSectionRef &RSR, StringRef Filename,
                       std::vector<std::string> &Duplicates);
  /// Remove conflicting manifest resources according to MinGW/MSVC policy.
  ///
  /// \param Duplicates Receives descriptions of manifests removed as duplicates.
  LLVM_ABI void cleanUpManifests(std::vector<std::string> &Duplicates);
  /// Print the parsed resource directory tree to \p OS.
  ///
  /// \param OS Stream that receives the tree dump.
  LLVM_ABI void printTree(raw_ostream &OS) const;
  /// Root of the parsed resource directory tree.
  ///
  /// \return A const reference to the root TreeNode.
  const TreeNode &getTree() const { return Root; }
  /// Resource data blobs indexed by data-node DataIndex values.
  ///
  /// \return The table of resource data payloads collected during parsing.
  ArrayRef<std::vector<uint8_t>> getData() const { return Data; }
  /// UTF-16 string table entries indexed by string-node StringIndex values.
  ///
  /// \return The table of UTF-16 strings collected during parsing.
  ArrayRef<std::vector<UTF16>> getStringTable() const { return StringTable; }

  /// Node in the resource directory tree (type, name, language, or data).
  class TreeNode {
  public:
    /// Map from child key \tparam T to owned child TreeNode.
    template <typename T>
    using Children = std::map<T, std::unique_ptr<TreeNode>>;

    /// Print this subtree to \p Writer under the label \p Name.
    ///
    /// \param Writer Scoped printer that receives the tree dump.
    /// \param Name Label for this node in the printed output.
    LLVM_ABI void print(ScopedPrinter &Writer, StringRef Name) const;
    /// Size in bytes of the COFF resource directory tree rooted at this node.
    ///
    /// \return The serialized directory-tree size in bytes.
    LLVM_ABI uint32_t getTreeSize() const;
    /// Index into the parser string table for this node's name, if string-keyed.
    ///
    /// \return The string-table index for this node's name.
    uint32_t getStringIndex() const { return StringIndex; }
    /// Index into the parser data table for this data node.
    ///
    /// \return The data-table index for this node's resource payload.
    uint32_t getDataIndex() const { return DataIndex; }
    /// Major version associated with this data node.
    ///
    /// \return The major version stored on this data node.
    uint16_t getMajorVersion() const { return MajorVersion; }
    /// Minor version associated with this data node.
    ///
    /// \return The minor version stored on this data node.
    uint16_t getMinorVersion() const { return MinorVersion; }
    /// Characteristics bits associated with this data node.
    ///
    /// \return The characteristics bits stored on this data node.
    uint32_t getCharacteristics() const { return Characteristics; }
    /// True if this node is a leaf that references resource data.
    ///
    /// \return True if this node is a data leaf; false if it has children.
    bool checkIsDataNode() const { return IsDataNode; }
    /// Children keyed by integer ID.
    ///
    /// \return A const reference to the map of ID-keyed child nodes.
    const Children<uint32_t> &getIDChildren() const { return IDChildren; }
    /// Children keyed by string name.
    ///
    /// \return A const reference to the map of string-keyed child nodes.
    const Children<std::string> &getStringChildren() const {
      return StringChildren;
    }

  private:
    friend class WindowsResourceParser;

    // Index is the StringTable vector index for this node's name.
    static std::unique_ptr<TreeNode> createStringNode(uint32_t Index);
    static std::unique_ptr<TreeNode> createIDNode();
    // DataIndex is the Data vector index that the data node points at.
    static std::unique_ptr<TreeNode> createDataNode(uint16_t MajorVersion,
                                                    uint16_t MinorVersion,
                                                    uint32_t Characteristics,
                                                    uint32_t Origin,
                                                    uint32_t DataIndex);

    explicit TreeNode(uint32_t StringIndex);
    TreeNode(uint16_t MajorVersion, uint16_t MinorVersion,
             uint32_t Characteristics, uint32_t Origin, uint32_t DataIndex);

    bool addEntry(const ResourceEntryRef &Entry, uint32_t Origin,
                  std::vector<std::vector<uint8_t>> &Data,
                  std::vector<std::vector<UTF16>> &StringTable,
                  TreeNode *&Result);
    TreeNode &addTypeNode(const ResourceEntryRef &Entry,
                          std::vector<std::vector<UTF16>> &StringTable);
    TreeNode &addNameNode(const ResourceEntryRef &Entry,
                          std::vector<std::vector<UTF16>> &StringTable);
    bool addLanguageNode(const ResourceEntryRef &Entry, uint32_t Origin,
                         std::vector<std::vector<uint8_t>> &Data,
                         TreeNode *&Result);
    bool addDataChild(uint32_t ID, uint16_t MajorVersion, uint16_t MinorVersion,
                      uint32_t Characteristics, uint32_t Origin,
                      uint32_t DataIndex, TreeNode *&Result);
    TreeNode &addIDChild(uint32_t ID);
    TreeNode &addNameChild(ArrayRef<UTF16> NameRef,
                           std::vector<std::vector<UTF16>> &StringTable);
    void shiftDataIndexDown(uint32_t Index);

    bool IsDataNode = false;
    uint32_t StringIndex;
    uint32_t DataIndex;
    Children<uint32_t> IDChildren;
    Children<std::string> StringChildren;
    uint16_t MajorVersion = 0;
    uint16_t MinorVersion = 0;
    uint32_t Characteristics = 0;

    // The .res file that defined this TreeNode, for diagnostics.
    // Index into InputFilenames.
    uint32_t Origin;
  };

  /// Either a UTF-16 string or an integer ID identifying a resource path component.
  struct StringOrID {
    /// True if this value is a string; false if it is an integer ID.
    bool IsString;
    /// UTF-16 string value when IsString is true.
    ArrayRef<UTF16> String;
    /// Integer ID when IsString is false (defaults to all-ones when unused).
    uint32_t ID = ~0u;

    /// Construct an ID-valued StringOrID with integer identifier \p ID.
    ///
    /// \param ID Integer resource path component.
    StringOrID(uint32_t ID) : IsString(false), ID(ID) {}
    /// Construct a string-valued StringOrID from UTF-16 sequence \p String.
    ///
    /// \param String UTF-16 characters of the resource path component.
    StringOrID(ArrayRef<UTF16> String) : IsString(true), String(String) {}
  };

private:
  Error addChildren(TreeNode &Node, ResourceSectionRef &RSR,
                    const coff_resource_dir_table &Table, uint32_t Origin,
                    std::vector<StringOrID> &Context,
                    std::vector<std::string> &Duplicates);
  bool shouldIgnoreDuplicate(const ResourceEntryRef &Entry) const;
  bool shouldIgnoreDuplicate(const std::vector<StringOrID> &Context) const;

  TreeNode Root;
  std::vector<std::vector<uint8_t>> Data;
  std::vector<std::vector<UTF16>> StringTable;

  std::vector<std::string> InputFilenames;

  bool MinGW;
};

/// Write a COFF object containing the resources from \p Parser.
///
/// \param MachineType Target COFF machine type for the output object.
/// \param Parser Parsed Windows resource directory and data to emit.
/// \param TimeDateStamp COFF TimeDateStamp value to write into the object.
/// \return A memory buffer with the COFF object bytes, or an error on failure.
LLVM_ABI Expected<std::unique_ptr<MemoryBuffer>>
writeWindowsResourceCOFF(llvm::COFF::MachineTypes MachineType,
                         const WindowsResourceParser &Parser,
                         uint32_t TimeDateStamp);

/// Print the standard name for predefined resource type ID \p TypeID to \p OS.
///
/// \param TypeID Predefined Windows resource type identifier.
/// \param OS Stream that receives the type name (or a generic ID label).
LLVM_ABI void printResourceTypeName(uint16_t TypeID, raw_ostream &OS);
} // namespace object
} // namespace llvm

#endif
