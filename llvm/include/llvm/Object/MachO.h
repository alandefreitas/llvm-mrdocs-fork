//===- MachO.h - MachO object file implementation ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MachOObjectFile class, which implement the ObjectFile
// interface for MachO files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_MACHO_H
#define LLVM_OBJECT_MACHO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/BinaryFormat/Swift.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>

namespace llvm {
namespace object {

/// DiceRef - This is a value type class that represents a single
/// data in code entry in the table in a Mach-O object file.
class DiceRef {
  DataRefImpl DicePimpl;
  const ObjectFile *OwningObject = nullptr;

public:
  /// Construct an empty DiceRef.
  DiceRef() = default;
  /// Construct a DiceRef from opaque data-ref \p DiceP in \p Owner.
  /// \param DiceP Opaque data-in-code entry reference.
  /// \param Owner Object file that owns the data-in-code table.
  DiceRef(DataRefImpl DiceP, const ObjectFile *Owner);

  /// Return true if this reference equals \p Other.
  /// \param Other DiceRef to compare against.
  /// @return True if this reference equals \p Other.
  bool operator==(const DiceRef &Other) const;
  /// Return true if this reference sorts before \p Other.
  /// \param Other DiceRef to compare against.
  /// @return True if this reference sorts before \p Other.
  bool operator<(const DiceRef &Other) const;

  /// Advance this reference to the next data-in-code entry.
  void moveNext();

  /// Return the file offset of this data-in-code entry.
  /// \param Result Set to the entry offset on success.
  /// @return Success, or an error code if the offset could not be read.
  std::error_code getOffset(uint32_t &Result) const;
  /// Return the length in bytes of this data-in-code entry.
  /// \param Result Set to the entry length on success.
  /// @return Success, or an error code if the length could not be read.
  std::error_code getLength(uint16_t &Result) const;
  /// Return the kind of this data-in-code entry.
  /// \param Result Set to the entry kind on success.
  /// @return Success, or an error code if the kind could not be read.
  std::error_code getKind(uint16_t &Result) const;

  /// Return the opaque data-ref for this entry.
  /// @return The opaque data-ref for this entry.
  DataRefImpl getRawDataRefImpl() const;
  /// Return the object file that owns this entry.
  /// @return The object file that owns this entry.
  const ObjectFile *getObjectFile() const;
};
/// Iterator over Mach-O data-in-code entries.
using dice_iterator = content_iterator<DiceRef>;

/// Cursor for a non-recursive walk of a Mach-O export trie.
///
/// ExportEntry encapsulates the current-state-of-the-walk used when doing a
/// non-recursive walk of the trie data structure.  This allows you to iterate
/// across all exported symbols using:
///      Error Err = Error::success();
///      for (const llvm::object::ExportEntry &AnExport : Obj->exports(&Err)) {
///      }
///      if (Err) { report error ...
class ExportEntry {
public:
  /// Construct an export-trie walker over \p Trie in object \p O.
  /// \param Err Set on decode errors encountered while iterating.
  /// \param O Optional owning Mach-O object.
  /// \param Trie Export-trie bytes to walk.
  LLVM_ABI ExportEntry(Error *Err, const MachOObjectFile *O,
                       ArrayRef<uint8_t> Trie);

  /// Return the accumulated export symbol name.
  /// @return The accumulated export symbol name.
  LLVM_ABI StringRef name() const;
  /// Return the export flags for the current node.
  /// @return The export flags for the current node.
  LLVM_ABI uint64_t flags() const;
  /// Return the export address for the current node.
  /// @return The export address for the current node.
  LLVM_ABI uint64_t address() const;
  /// Return the "other" field for the current export node.
  /// @return The "other" field for the current export node.
  LLVM_ABI uint64_t other() const;
  /// Return the other/import name for the current export node, if any.
  /// @return The other/import name for the current export node, if any.
  LLVM_ABI StringRef otherName() const;
  /// Return the trie-node offset of the current export.
  /// @return The trie-node offset of the current export.
  LLVM_ABI uint32_t nodeOffset() const;

  /// Return true if this entry equals \p Other.
  /// \param Other Export entry to compare against.
  /// @return True if this entry equals \p Other.
  LLVM_ABI bool operator==(const ExportEntry &Other) const;

  /// Advance to the next exported symbol.
  LLVM_ABI void moveNext();

private:
  friend class MachOObjectFile;

  void moveToFirst();
  void moveToEnd();
  uint64_t readULEB128(const uint8_t *&p, const char **error);
  void pushDownUntilBottom();
  void pushNode(uint64_t Offset);

  // Represents a node in the mach-o exports trie.
  struct NodeState {
    LLVM_ABI NodeState(const uint8_t *Ptr);

    const uint8_t *Start;
    const uint8_t *Current;
    uint64_t Flags = 0;
    uint64_t Address = 0;
    uint64_t Other = 0;
    const char *ImportName = nullptr;
    unsigned ChildCount = 0;
    unsigned NextChildIndex = 0;
    unsigned ParentStringLength = 0;
    bool IsExportNode = false;
  };
  using NodeList = SmallVector<NodeState, 16>;
  using node_iterator = NodeList::const_iterator;

  Error *E;
  const MachOObjectFile *O;
  ArrayRef<uint8_t> Trie;
  SmallString<256> CumulativeString;
  NodeList Stack;
  bool Done = false;

  iterator_range<node_iterator> nodes() const { return Stack; }
};
/// Iterator over Mach-O export-trie entries.
using export_iterator = content_iterator<ExportEntry>;

/// Segment/section lookup table for validating Mach-O bind and rebase entries.
///
/// Segment info so SegIndex/SegOffset pairs in a Mach-O Bind or Rebase entry
/// can be checked and translated.  Only the SegIndex/SegOffset pairs from
/// checked entries are to be used with the segmentName(), sectionName() and
/// address() methods below.
class BindRebaseSegInfo {
public:
  /// Construct segment info for Mach-O object \p Obj.
  /// \param Obj Mach-O object whose segments and sections are indexed.
  LLVM_ABI BindRebaseSegInfo(const MachOObjectFile *Obj);

  /// Check that a bind/rebase location falls within a mapped section.
  ///
  /// Used to check a Mach-O Bind or Rebase entry for errors when iterating.
  ///
  /// \param SegIndex Segment index to validate.
  /// \param SegOffset Offset within the segment of the first pointer.
  /// \param PointerSize Size in bytes of each pointer.
  /// \param Count Number of pointers in the sequence.
  /// \param Skip Bytes to skip between successive pointers.
  /// @return Null on success, or an error message if the location is invalid.
  LLVM_ABI const char *checkSegAndOffsets(int32_t SegIndex, uint64_t SegOffset,
                                          uint8_t PointerSize,
                                          uint64_t Count = 1,
                                          uint64_t Skip = 0);
  /// Return the segment name for validated segment index \p SegIndex.
  ///
  /// Used with valid SegIndex/SegOffset values from checked entries.
  ///
  /// \param SegIndex Validated segment index.
  /// @return The segment name for validated segment index \p SegIndex.
  LLVM_ABI StringRef segmentName(int32_t SegIndex);
  /// Return the section name containing \p SegIndex/\p SegOffset.
  /// \param SegIndex Validated segment index.
  /// \param SegOffset Validated offset within the segment.
  /// @return The section name containing \p SegIndex/\p SegOffset.
  LLVM_ABI StringRef sectionName(int32_t SegIndex, uint64_t SegOffset);
  /// Return the VM address for validated \p SegIndex/\p SegOffset.
  /// \param SegIndex Validated segment index.
  /// \param SegOffset Validated offset within the segment.
  /// @return The VM address for validated \p SegIndex/\p SegOffset.
  LLVM_ABI uint64_t address(uint32_t SegIndex, uint64_t SegOffset);

private:
  struct SectionInfo {
    uint64_t Address;
    uint64_t Size;
    StringRef SectionName;
    StringRef SegmentName;
    uint64_t OffsetInSegment;
    uint64_t SegmentStartAddress;
    int32_t SegmentIndex;
  };
  const SectionInfo &findSection(int32_t SegIndex, uint64_t SegOffset);

  SmallVector<SectionInfo, 32> Sections;
  int32_t MaxSegIndex;
};

/// Decompression cursor for Mach-O dyld rebase opcodes.
///
/// MachORebaseEntry encapsulates the current state in the decompression of
/// rebasing opcodes. This allows you to iterate through the compressed table of
/// rebasing using:
///    Error Err = Error::success();
///    for (const llvm::object::MachORebaseEntry &Entry : Obj->rebaseTable(&Err)) {
///    }
///    if (Err) { report error ...
class MachORebaseEntry {
public:
  /// Construct a rebase-entry walker over \p opcodes in object \p O.
  /// \param Err Set on decode errors encountered while iterating.
  /// \param O Mach-O object providing segment/section translation.
  /// \param opcodes Rebase opcode bytes to decompress.
  /// \param is64Bit True when decoding 64-bit pointer opcodes.
  LLVM_ABI MachORebaseEntry(Error *Err, const MachOObjectFile *O,
                            ArrayRef<uint8_t> opcodes, bool is64Bit);

  /// Return the segment index of the current rebase.
  /// @return The segment index of the current rebase.
  LLVM_ABI int32_t segmentIndex() const;
  /// Return the offset of the current rebase within its segment.
  /// @return The offset of the current rebase within its segment.
  LLVM_ABI uint64_t segmentOffset() const;
  /// Return a display name for the current rebase type.
  /// @return A display name for the current rebase type.
  LLVM_ABI StringRef typeName() const;
  /// Return the segment name of the current rebase.
  /// @return The segment name of the current rebase.
  LLVM_ABI StringRef segmentName() const;
  /// Return the section name of the current rebase.
  /// @return The section name of the current rebase.
  LLVM_ABI StringRef sectionName() const;
  /// Return the VM address of the current rebase.
  /// @return The VM address of the current rebase.
  LLVM_ABI uint64_t address() const;

  /// Return true if this entry equals \p Other.
  /// \param Other Rebase entry to compare against.
  /// @return True if this entry equals \p Other.
  LLVM_ABI bool operator==(const MachORebaseEntry &Other) const;

  /// Advance to the next rebase table entry.
  LLVM_ABI void moveNext();

private:
  friend class MachOObjectFile;

  void moveToFirst();
  void moveToEnd();
  uint64_t readULEB128(const char **error);

  Error *E;
  const MachOObjectFile *O;
  ArrayRef<uint8_t> Opcodes;
  const uint8_t *Ptr;
  uint64_t SegmentOffset = 0;
  int32_t SegmentIndex = -1;
  uint64_t RemainingLoopCount = 0;
  uint64_t AdvanceAmount = 0;
  uint8_t  RebaseType = 0;
  uint8_t  PointerSize;
  bool     Done = false;
};
/// Iterator over Mach-O dyld rebase table entries.
using rebase_iterator = content_iterator<MachORebaseEntry>;

/// Decompression cursor for Mach-O dyld bind opcodes.
///
/// MachOBindEntry encapsulates the current state in the decompression of
/// binding opcodes. This allows you to iterate through the compressed table of
/// bindings using:
///    Error Err = Error::success();
///    for (const llvm::object::MachOBindEntry &Entry : Obj->bindTable(&Err)) {
///    }
///    if (Err) { report error ...
class MachOBindEntry {
public:
  /// Which bind opcode table this entry is walking.
  enum class Kind {
    Regular, ///< Ordinary bind table.
    Lazy,    ///< Lazy bind table.
    Weak,    ///< Weak bind table.
  };

  /// Construct a bind-entry walker over \p Opcodes in object \p O.
  /// \param Err Set on decode errors encountered while iterating.
  /// \param O Mach-O object providing segment/section translation.
  /// \param Opcodes Bind opcode bytes to decompress.
  /// \param is64Bit True when decoding 64-bit pointer opcodes.
  /// \param TableKind Which bind table flavor these opcodes describe.
  LLVM_ABI MachOBindEntry(Error *Err, const MachOObjectFile *O,
                          ArrayRef<uint8_t> Opcodes, bool is64Bit,
                          MachOBindEntry::Kind TableKind);

  /// Return the segment index of the current bind.
  /// @return The segment index of the current bind.
  LLVM_ABI int32_t segmentIndex() const;
  /// Return the offset of the current bind within its segment.
  /// @return The offset of the current bind within its segment.
  LLVM_ABI uint64_t segmentOffset() const;
  /// Return a display name for the current bind type.
  /// @return A display name for the current bind type.
  LLVM_ABI StringRef typeName() const;
  /// Return the symbol name of the current bind.
  /// @return The symbol name of the current bind.
  LLVM_ABI StringRef symbolName() const;
  /// Return the flags of the current bind.
  /// @return The flags of the current bind.
  LLVM_ABI uint32_t flags() const;
  /// Return the addend of the current bind.
  /// @return The addend of the current bind.
  LLVM_ABI int64_t addend() const;
  /// Return the library ordinal of the current bind.
  /// @return The library ordinal of the current bind.
  LLVM_ABI int ordinal() const;

  /// Return the segment name of the current bind.
  /// @return The segment name of the current bind.
  LLVM_ABI StringRef segmentName() const;
  /// Return the section name of the current bind.
  /// @return The section name of the current bind.
  LLVM_ABI StringRef sectionName() const;
  /// Return the VM address of the current bind.
  /// @return The VM address of the current bind.
  LLVM_ABI uint64_t address() const;

  /// Return true if this entry equals \p Other.
  /// \param Other Bind entry to compare against.
  /// @return True if this entry equals \p Other.
  LLVM_ABI bool operator==(const MachOBindEntry &Other) const;

  /// Advance to the next bind table entry.
  LLVM_ABI void moveNext();

private:
  friend class MachOObjectFile;

  void moveToFirst();
  void moveToEnd();
  uint64_t readULEB128(const char **error);
  int64_t readSLEB128(const char **error);

  Error *E;
  const MachOObjectFile *O;
  ArrayRef<uint8_t> Opcodes;
  const uint8_t *Ptr;
  uint64_t SegmentOffset = 0;
  int32_t  SegmentIndex = -1;
  StringRef SymbolName;
  bool     LibraryOrdinalSet = false;
  int      Ordinal = 0;
  uint32_t Flags = 0;
  int64_t  Addend = 0;
  uint64_t RemainingLoopCount = 0;
  uint64_t AdvanceAmount = 0;
  uint8_t  BindType = 0;
  uint8_t  PointerSize;
  Kind     TableKind;
  bool     Done = false;
};
/// Iterator over Mach-O dyld bind table entries.
using bind_iterator = content_iterator<MachOBindEntry>;

/// External symbol target referenced by chained fixup binds.
///
/// ChainedFixupTarget holds all the information about an external symbol
/// necessary to bind this binary to that symbol. These values are referenced
/// indirectly by chained fixup binds. This structure captures values from all
/// import and symbol formats.
///
/// Be aware there are two notions of weak here:
///   WeakImport == true
///     The associated bind may be set to 0 if this symbol is missing from its
///     parent library. This is called a "weak import."
///   LibOrdinal == BIND_SPECIAL_DYLIB_WEAK_LOOKUP
///     This symbol may be coalesced with other libraries vending the same
///     symbol. E.g., C++'s "operator new". This is called a "weak bind."
struct ChainedFixupTarget {
public:
  /// Construct a chained fixup target description.
  /// \param LibOrdinal Library ordinal of the providing dylib.
  /// \param NameOffset Offset of the symbol name in the imports name table.
  /// \param Symbol Symbol name string.
  /// \param Addend Addend applied when binding.
  /// \param WeakImport True if this is a weak import.
  ChainedFixupTarget(int LibOrdinal, uint32_t NameOffset, StringRef Symbol,
                     uint64_t Addend, bool WeakImport)
      : LibOrdinal(LibOrdinal), NameOffset(NameOffset), SymbolName(Symbol),
        Addend(Addend), WeakImport(WeakImport) {}

  /// Return the library ordinal for this target.
  /// @return The library ordinal for this target.
  int libOrdinal() { return LibOrdinal; }
  /// Return the symbol-name table offset for this target.
  /// @return The symbol-name table offset for this target.
  uint32_t nameOffset() { return NameOffset; }
  /// Return the symbol name for this target.
  /// @return The symbol name for this target.
  StringRef symbolName() { return SymbolName; }
  /// Return the addend for this target.
  /// @return The addend for this target.
  uint64_t addend() { return Addend; }
  /// Return true if this target is a weak import.
  /// @return True if this target is a weak import.
  bool weakImport() { return WeakImport; }
  /// Return true if this target uses weak-bind lookup.
  /// @return True if this target uses weak-bind lookup.
  bool weakBind() {
    return LibOrdinal == MachO::BIND_SPECIAL_DYLIB_WEAK_LOOKUP;
  }

private:
  int LibOrdinal;
  uint32_t NameOffset;
  StringRef SymbolName;
  uint64_t Addend;
  bool WeakImport;
};

/// Per-segment metadata describing dyld chained fixup page starts.
struct ChainedFixupsSegment {
  /// Construct chained-fixup segment metadata.
  /// \param SegIdx Segment index in the image.
  /// \param Offset File offset of this segment's starts info.
  /// \param Header Starts-in-segment header.
  /// \param PageStarts Host-endian page_start[] entries.
  ChainedFixupsSegment(uint8_t SegIdx, uint32_t Offset,
                       const MachO::dyld_chained_starts_in_segment &Header,
                       std::vector<uint16_t> &&PageStarts)
      : SegIdx(SegIdx), Offset(Offset), Header(Header),
        PageStarts(PageStarts){};

  uint32_t SegIdx;  ///< Segment index in the image.
  uint32_t Offset;  ///< dyld_chained_starts_in_image::seg_info_offset[SegIdx]
  MachO::dyld_chained_starts_in_segment Header; ///< Starts-in-segment header.
  std::vector<uint16_t> PageStarts; ///< page_start[] entries, host endianness
};

/// Abstract base for a fixup in a MH_DYLDLINK Mach-O file.
///
/// MachOAbstractFixupEntry is an abstract class representing a fixup in a
/// MH_DYLDLINK file. Fixups generally represent rebases and binds. Binds also
/// subdivide into additional subtypes (weak, lazy, reexport).
///
/// The two concrete subclasses of MachOAbstractFixupEntry are:
///
///   MachORebaseBindEntry   - for dyld opcode-based tables, including threaded-
///                            rebase, where rebases are mixed in with other
///                            bind opcodes.
///   MachOChainedFixupEntry - for pointer chains embedded in data pages.
class MachOAbstractFixupEntry {
public:
  /// Construct an abstract fixup entry for object \p O.
  /// \param Err Set on decode errors encountered while iterating.
  /// \param O Mach-O object providing segment/section context.
  LLVM_ABI MachOAbstractFixupEntry(Error *Err, const MachOObjectFile *O);

  /// Return the segment index of this fixup.
  /// @return The segment index of this fixup.
  LLVM_ABI int32_t segmentIndex() const;
  /// Return the offset of this fixup within its segment.
  /// @return The offset of this fixup within its segment.
  LLVM_ABI uint64_t segmentOffset() const;
  /// Return the VM address of the start of this fixup's segment.
  /// @return The VM address of the start of this fixup's segment.
  LLVM_ABI uint64_t segmentAddress() const;
  /// Return the name of this fixup's segment.
  /// @return The name of this fixup's segment.
  LLVM_ABI StringRef segmentName() const;
  /// Return the name of this fixup's section.
  /// @return The name of this fixup's section.
  LLVM_ABI StringRef sectionName() const;
  /// Return a display name for this fixup's type.
  /// @return A display name for this fixup's type.
  LLVM_ABI StringRef typeName() const;
  /// Return the symbol name associated with this fixup, if any.
  /// @return The symbol name associated with this fixup, if any.
  LLVM_ABI StringRef symbolName() const;
  /// Return the bind/rebase flags for this fixup.
  /// @return The bind/rebase flags for this fixup.
  LLVM_ABI uint32_t flags() const;
  /// Return the addend for this fixup.
  /// @return The addend for this fixup.
  LLVM_ABI int64_t addend() const;
  /// Return the library ordinal for this fixup.
  /// @return The library ordinal for this fixup.
  LLVM_ABI int ordinal() const;

  /// Return the VM address where this fixup is located.
  ///
  /// For the VM Address this fixup is pointing to, use pointerValue().
  /// @return The VM address where this fixup is located.
  LLVM_ABI uint64_t address() const;

  /// Return the VM address pointed to by this fixup.
  ///
  /// Use pointerValue() to compare against other VM Addresses, such as
  /// section addresses or segment vmaddrs.
  /// @return The VM address pointed to by this fixup.
  uint64_t pointerValue() const { return PointerValue; }

  /// Return the raw on-disk encoding of this fixup.
  ///
  /// For Threaded rebases and Chained pointers these values are generally
  /// encoded into various different pointer formats. This value is
  /// exposed in API for tools that want to display and annotate the
  /// raw bits.
  /// @return The raw on-disk encoding of this fixup.
  uint64_t rawValue() const { return RawValue; }

  /// Advance to the next fixup entry.
  LLVM_ABI void moveNext();

protected:
  Error *E;                       ///< Error sink for decode failures.
  const MachOObjectFile *O;       ///< Owning Mach-O object.
  uint64_t SegmentOffset = 0;     ///< Offset of this fixup within its segment.
  int32_t SegmentIndex = -1;      ///< Segment index of this fixup.
  StringRef SymbolName;           ///< Bound symbol name, if any.
  int32_t Ordinal = 0;            ///< Library ordinal for binds.
  uint32_t Flags = 0;             ///< Bind/rebase flags.
  int64_t Addend = 0;             ///< Addend applied by this fixup.
  uint64_t PointerValue = 0;      ///< VM address pointed to by this fixup.
  uint64_t RawValue = 0;          ///< Raw encoded fixup bits.
  bool Done = false;              ///< True when iteration is finished.

  /// Position this entry at the first fixup.
  LLVM_ABI void moveToFirst();
  /// Position this entry past the last fixup.
  LLVM_ABI void moveToEnd();

  /// Return the VM address of the start of the __TEXT segment.
  /// @return The VM address of the start of the __TEXT segment.
  uint64_t textAddress() const { return TextAddress; }

private:
  uint64_t TextAddress;
};

/// Concrete fixup entry for dyld chained pointer fixups.
class MachOChainedFixupEntry : public MachOAbstractFixupEntry {
public:
  /// Kind of chained fixup represented by this entry.
  enum class FixupKind {
    Bind,   ///< External symbol bind.
    Rebase, ///< Internal rebase.
  };

  /// Construct a chained-fixup entry walker for object \p O.
  /// \param Err Set on decode errors encountered while iterating.
  /// \param O Mach-O object containing chained fixups.
  /// \param Parse If true, parse fixup data immediately.
  LLVM_ABI MachOChainedFixupEntry(Error *Err, const MachOObjectFile *O,
                                  bool Parse);

  /// Return true if this entry equals \p Other.
  /// \param Other Chained fixup entry to compare against.
  /// @return True if this entry equals \p Other.
  LLVM_ABI bool operator==(const MachOChainedFixupEntry &Other) const;

  /// Return true if this entry is a bind.
  /// @return True if this entry is a bind.
  bool isBind() const { return Kind == FixupKind::Bind; }
  /// Return true if this entry is a rebase.
  /// @return True if this entry is a rebase.
  bool isRebase() const { return Kind == FixupKind::Rebase; }

  /// Advance to the next chained fixup entry.
  LLVM_ABI void moveNext();
  /// Position this entry at the first chained fixup.
  LLVM_ABI void moveToFirst();
  /// Position this entry past the last chained fixup.
  LLVM_ABI void moveToEnd();

private:
  void findNextPageWithFixups();

  std::vector<ChainedFixupTarget> FixupTargets;
  std::vector<ChainedFixupsSegment> Segments;
  ArrayRef<uint8_t> SegmentData;
  FixupKind Kind;
  uint32_t InfoSegIndex = 0; // Index into Segments
  uint32_t PageIndex = 0;    // Index into Segments[InfoSegIdx].PageStarts
  uint32_t PageOffset = 0;   // Page offset of the current fixup
};
/// Iterator over Mach-O chained fixup entries.
using fixup_iterator = content_iterator<MachOChainedFixupEntry>;

class LLVM_ABI MachOObjectFile : public ObjectFile {
public:
  /// Describes one Mach-O load command and its location in the file buffer.
  struct LoadCommandInfo {
    const char *Ptr;       ///< Where in memory the load command is.
    MachO::load_command C; ///< The command itself.
  };
  /// List of parsed load-command descriptors for this object.
  using LoadCommandList = SmallVector<LoadCommandInfo, 4>;
  /// Const iterator over LoadCommandList entries.
  using load_command_iterator = LoadCommandList::const_iterator;

  /// Create a MachOObjectFile from buffer \p Object.
  /// \param Object Memory buffer containing the Mach-O image.
  /// \param IsLittleEndian True if the image is little-endian.
  /// \param Is64Bits True if the image is 64-bit Mach-O.
  /// \param UniversalCputype Optional CPU type when opened from a universal binary.
  /// \param UniversalIndex Optional slice index when opened from a universal binary.
  /// \param MachOFilesetEntryOffset Optional fileset entry offset for MH_FILESET.
  /// @return The new MachOObjectFile, or an error if parsing fails.
  static Expected<std::unique_ptr<MachOObjectFile>>
  create(MemoryBufferRef Object, bool IsLittleEndian, bool Is64Bits,
         uint32_t UniversalCputype = 0, uint32_t UniversalIndex = 0,
         size_t MachOFilesetEntryOffset = 0);

  /// Return true if \p RelocType is a paired Mach-O relocation for \p Arch.
  /// \param RelocType Relocation type code.
  /// \param Arch Architecture for which to interpret \p RelocType.
  /// @return True if \p RelocType is a paired Mach-O relocation for \p Arch.
  static bool isMachOPairedReloc(uint64_t RelocType, uint64_t Arch);

  /// Advance symbol iterator \p Symb to the next symbol.
  /// \param Symb Opaque symbol iterator state to advance.
  void moveSymbolNext(DataRefImpl &Symb) const override;

  /// Return the n_value field of symbol \p Sym.
  /// \param Sym Opaque symbol reference.
  /// @return The n_value field of symbol \p Sym.
  uint64_t getNValue(DataRefImpl Sym) const;
  /// Return the name of symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The name of symbol \p Symb.
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;

  // MachO specific.
  /// Validate the symbol table and return an error if it is malformed.
  /// @return Success, or an error if the symbol table is malformed.
  Error checkSymbolTable() const;

  /// Return the indirect symbol name for symbol \p Symb, if any.
  /// \param Symb Opaque symbol reference.
  /// \param Res Set to the indirect name on success.
  /// @return Success, or an error code if no indirect name is available.
  std::error_code getIndirectName(DataRefImpl Symb, StringRef &Res) const;
  /// Return the Mach-O section type bits for section \p Sec.
  /// \param Sec Section whose type is requested.
  /// @return The Mach-O section type bits for section \p Sec.
  unsigned getSectionType(SectionRef Sec) const;

  /// Return the virtual address of symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The virtual address of symbol \p Symb.
  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  /// Return the alignment of symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The alignment of symbol \p Symb.
  uint32_t getSymbolAlignment(DataRefImpl Symb) const override;
  /// Return the size of common symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The size of common symbol \p Symb.
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;
  /// Return the SymbolRef type of symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The SymbolRef type of symbol \p Symb.
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const override;
  /// Return SymbolRef flags for symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return SymbolRef flags for symbol \p Symb.
  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override;
  /// Return the section containing symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The section containing symbol \p Symb.
  Expected<section_iterator> getSymbolSection(DataRefImpl Symb) const override;
  /// Return the section ID owning symbol \p Symb.
  /// \param Symb Symbol whose section ID is requested.
  /// @return The section ID owning symbol \p Symb.
  unsigned getSymbolSectionID(SymbolRef Symb) const;
  /// Return the section ID of section \p Sec.
  /// \param Sec Section whose ID is requested.
  /// @return The section ID of section \p Sec.
  unsigned getSectionID(SectionRef Sec) const;

  /// Advance section iterator \p Sec to the next section.
  /// \param Sec Opaque section iterator state to advance.
  void moveSectionNext(DataRefImpl &Sec) const override;
  /// Return the name of section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The name of section \p Sec.
  Expected<StringRef> getSectionName(DataRefImpl Sec) const override;
  /// Return the address of section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The address of section \p Sec.
  uint64_t getSectionAddress(DataRefImpl Sec) const override;
  /// Return the zero-based index of section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The zero-based index of section \p Sec.
  uint64_t getSectionIndex(DataRefImpl Sec) const override;
  /// Return the size of section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The size of section \p Sec.
  uint64_t getSectionSize(DataRefImpl Sec) const override;
  /// Return \p Size bytes of section contents starting at file offset \p Offset.
  /// \param Offset File offset of the contents.
  /// \param Size Number of bytes to return.
  /// @return \p Size bytes of section contents starting at file offset \p Offset.
  ArrayRef<uint8_t> getSectionContents(uint64_t Offset, uint64_t Size) const;
  /// Return the contents of section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The contents of section \p Sec.
  Expected<ArrayRef<uint8_t>>
  getSectionContents(DataRefImpl Sec) const override;
  /// Return the alignment of section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The alignment of section \p Sec.
  uint64_t getSectionAlignment(DataRefImpl Sec) const override;
  /// Return the section at zero-based index \p SectionIndex.
  /// \param SectionIndex Zero-based section index.
  /// @return The section at zero-based index \p SectionIndex.
  Expected<SectionRef> getSection(unsigned SectionIndex) const;
  /// Return the section named \p SectionName.
  /// \param SectionName Section name to look up.
  /// @return The section named \p SectionName.
  Expected<SectionRef> getSection(StringRef SectionName) const;
  /// Return true if section \p Sec is compressed.
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec is compressed.
  bool isSectionCompressed(DataRefImpl Sec) const override;
  /// Return true if section \p Sec contains code.
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec contains code.
  bool isSectionText(DataRefImpl Sec) const override;
  /// Return true if section \p Sec contains initialized data.
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec contains initialized data.
  bool isSectionData(DataRefImpl Sec) const override;
  /// Return true if section \p Sec is BSS/uninitialized.
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec is BSS/uninitialized.
  bool isSectionBSS(DataRefImpl Sec) const override;
  /// Return true if section \p Sec has no file contents (virtual).
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec has no file contents (virtual).
  bool isSectionVirtual(DataRefImpl Sec) const override;
  /// Return true if section \p Sec contains LLVM bitcode.
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec contains LLVM bitcode.
  bool isSectionBitcode(DataRefImpl Sec) const override;
  /// Return true if section \p Sec is a debug section.
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec is a debug section.
  bool isDebugSection(DataRefImpl Sec) const override;

  /// Return the raw contents of an entire segment by name.
  /// \param SegmentName Segment name to look up.
  /// @return The raw contents of an entire segment by name.
  ArrayRef<uint8_t> getSegmentContents(StringRef SegmentName) const;
  /// Return the raw contents of segment index \p SegmentIndex.
  /// \param SegmentIndex Zero-based segment index.
  /// @return The raw contents of segment index \p SegmentIndex.
  ArrayRef<uint8_t> getSegmentContents(size_t SegmentIndex) const;

  /// Return true if section \p Sec was stripped by dsymutil.
  ///
  /// When dsymutil generates the companion file, it strips all unnecessary
  /// sections (e.g. everything in the _TEXT segment) by omitting their body
  /// and setting the offset in their corresponding load command to zero.
  ///
  /// While the load command itself is valid, reading the section corresponds
  /// to reading the number of bytes specified in the load command, starting
  /// from offset 0 (i.e. the Mach-O header at the beginning of the file).
  ///
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec was stripped by dsymutil.
  bool isSectionStripped(DataRefImpl Sec) const override;

  /// Return an iterator to the first relocation in section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return An iterator to the first relocation in section \p Sec.
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override;
  /// Return an iterator past the last relocation in section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return An iterator past the last relocation in section \p Sec.
  relocation_iterator section_rel_end(DataRefImpl Sec) const override;

  /// Return an iterator to the first external relocation.
  /// @return An iterator to the first external relocation.
  relocation_iterator extrel_begin() const;
  /// Return an iterator past the last external relocation.
  /// @return An iterator past the last external relocation.
  relocation_iterator extrel_end() const;
  /// Return the range of external relocations.
  /// @return The range of external relocations.
  iterator_range<relocation_iterator> external_relocations() const {
    return make_range(extrel_begin(), extrel_end());
  }

  /// Return an iterator to the first local relocation.
  /// @return An iterator to the first local relocation.
  relocation_iterator locrel_begin() const;
  /// Return an iterator past the last local relocation.
  /// @return An iterator past the last local relocation.
  relocation_iterator locrel_end() const;

  /// Advance relocation iterator \p Rel to the next relocation.
  /// \param Rel Opaque relocation iterator state to advance.
  void moveRelocationNext(DataRefImpl &Rel) const override;
  /// Return the section-relative offset of relocation \p Rel.
  /// \param Rel Opaque relocation reference.
  /// @return The section-relative offset of relocation \p Rel.
  uint64_t getRelocationOffset(DataRefImpl Rel) const override;
  /// Return the symbol referenced by relocation \p Rel.
  /// \param Rel Opaque relocation reference.
  /// @return The symbol referenced by relocation \p Rel.
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override;
  /// Return the section associated with relocation \p Rel.
  /// \param Rel Opaque relocation reference.
  /// @return The section associated with relocation \p Rel.
  section_iterator getRelocationSection(DataRefImpl Rel) const;
  /// Return the relocation type of \p Rel.
  /// \param Rel Opaque relocation reference.
  /// @return The relocation type of \p Rel.
  uint64_t getRelocationType(DataRefImpl Rel) const override;
  /// Append the relocation type name of \p Rel to \p Result.
  /// \param Rel Opaque relocation reference.
  /// \param Result Output buffer for the type name.
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override;
  /// Return the length field of relocation \p Rel.
  /// \param Rel Opaque relocation reference.
  /// @return The length field of relocation \p Rel.
  uint8_t getRelocationLength(DataRefImpl Rel) const;

  // MachO specific.
  /// Return the short library name for dependent library index \p Index.
  /// \param Index Zero-based library index.
  /// \param Res Set to the short library name on success.
  /// @return Success, or an error code if the library name could not be read.
  std::error_code getLibraryShortNameByIndex(unsigned Index,
                                             StringRef &Res) const;
  /// Return the number of dependent libraries recorded in this object.
  /// @return The number of dependent libraries recorded in this object.
  uint32_t getLibraryCount() const;

  /// Return the section that relocation \p Rel relocates.
  /// \param Rel Relocation iterator identifying the relocation.
  /// @return The section that relocation \p Rel relocates.
  section_iterator getRelocationRelocatedSection(relocation_iterator Rel) const;

  // TODO: Would be useful to have an iterator based version
  // of the load command interface too.

  /// Return an iterator to the first symbol.
  /// @return An iterator to the first symbol.
  basic_symbol_iterator symbol_begin() const override;
  /// Return an iterator past the last symbol.
  /// @return An iterator past the last symbol.
  basic_symbol_iterator symbol_end() const override;

  /// Return true if this is a 64-bit Mach-O object.
  /// @return True if this is a 64-bit Mach-O object.
  bool is64Bit() const override;

  // MachO specific.
  /// Return a symbol iterator for symbol table index \p Index.
  /// \param Index Zero-based symbol table index.
  /// @return A symbol iterator for symbol table index \p Index.
  symbol_iterator getSymbolByIndex(unsigned Index) const;
  /// Return the symbol-table index of opaque symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The symbol-table index of opaque symbol \p Symb.
  uint64_t getSymbolIndex(DataRefImpl Symb) const;

  /// Return an iterator to the first section.
  /// @return An iterator to the first section.
  section_iterator section_begin() const override;
  /// Return an iterator past the last section.
  /// @return An iterator past the last section.
  section_iterator section_end() const override;

  /// Return the address size in bytes for this object.
  /// @return The address size in bytes for this object.
  uint8_t getBytesInAddress() const override;

  /// Return a human-readable Mach-O file format name.
  /// @return A human-readable Mach-O file format name.
  StringRef getFileFormatName() const override;
  /// Return the Triple::ArchType for this object.
  /// @return The Triple::ArchType for this object.
  Triple::ArchType getArch() const override;
  /// Return subtarget features (always empty for Mach-O).
  /// @return Subtarget features (always empty for Mach-O).
  Expected<SubtargetFeatures> getFeatures() const override {
    return SubtargetFeatures();
  }
  /// Return an architecture Triple for this object.
  /// \param McpuDefault Optional output for a default -mcpu string.
  /// @return An architecture Triple for this object.
  Triple getArchTriple(const char **McpuDefault = nullptr) const;

  /// Return an iterator to the first relocation in section index \p Index.
  /// \param Index Zero-based section index.
  /// @return An iterator to the first relocation in section index \p Index.
  relocation_iterator section_rel_begin(unsigned Index) const;
  /// Return an iterator past the last relocation in section index \p Index.
  /// \param Index Zero-based section index.
  /// @return An iterator past the last relocation in section index \p Index.
  relocation_iterator section_rel_end(unsigned Index) const;

  /// Return an iterator to the first data-in-code entry.
  /// @return An iterator to the first data-in-code entry.
  dice_iterator begin_dices() const;
  /// Return an iterator past the last data-in-code entry.
  /// @return An iterator past the last data-in-code entry.
  dice_iterator end_dices() const;

  /// Return an iterator to the first load command.
  /// @return An iterator to the first load command.
  load_command_iterator begin_load_commands() const;
  /// Return an iterator past the last load command.
  /// @return An iterator past the last load command.
  load_command_iterator end_load_commands() const;
  /// Return the range of load commands in this object.
  /// @return The range of load commands in this object.
  iterator_range<load_command_iterator> load_commands() const;

  /// For use iterating over all exported symbols.
  /// \param Err Set on decode errors encountered while iterating.
  /// @return A range of export-trie entries for this object.
  iterator_range<export_iterator> exports(Error &Err) const;

  /// For use examining a trie not in a MachOObjectFile.
  /// \param Err Set on decode errors encountered while iterating.
  /// \param Trie Export-trie bytes to walk.
  /// \param O Optional owning object used for address translation.
  /// @return A range of export-trie entries for the given trie.
  static iterator_range<export_iterator> exports(Error &Err,
                                                 ArrayRef<uint8_t> Trie,
                                                 const MachOObjectFile *O =
                                                                      nullptr);

  /// For use iterating over all rebase table entries.
  /// \param Err Set on decode errors encountered while iterating.
  /// @return A range of rebase table entries for this object.
  iterator_range<rebase_iterator> rebaseTable(Error &Err);

  /// For use examining rebase opcodes in a MachOObjectFile.
  /// \param Err Set on decode errors encountered while iterating.
  /// \param O Object providing segment/section translation.
  /// \param Opcodes Rebase opcode bytes to walk.
  /// \param is64 True when decoding 64-bit pointer opcodes.
  /// @return A range of rebase table entries for the given opcodes.
  static iterator_range<rebase_iterator> rebaseTable(Error &Err,
                                                     MachOObjectFile *O,
                                                     ArrayRef<uint8_t> Opcodes,
                                                     bool is64);

  /// For use iterating over all bind table entries.
  /// \param Err Set on decode errors encountered while iterating.
  /// @return A range of bind table entries for this object.
  iterator_range<bind_iterator> bindTable(Error &Err);

  /// For iterating over all chained fixups.
  /// \param Err Set on decode errors encountered while iterating.
  /// @return A range of chained fixup entries for this object.
  iterator_range<fixup_iterator> fixupTable(Error &Err);

  /// For use iterating over all lazy bind table entries.
  /// \param Err Set on decode errors encountered while iterating.
  /// @return A range of lazy bind table entries for this object.
  iterator_range<bind_iterator> lazyBindTable(Error &Err);

  /// For use iterating over all weak bind table entries.
  /// \param Err Set on decode errors encountered while iterating.
  /// @return A range of weak bind table entries for this object.
  iterator_range<bind_iterator> weakBindTable(Error &Err);

  /// For use examining bind opcodes in a MachOObjectFile.
  /// \param Err Set on decode errors encountered while iterating.
  /// \param O Object providing segment/section translation.
  /// \param Opcodes Bind opcode bytes to walk.
  /// \param is64 True when decoding 64-bit pointer opcodes.
  /// \param TableKind Which bind table flavor these opcodes describe.
  /// @return A range of bind table entries for the given opcodes.
  static iterator_range<bind_iterator> bindTable(Error &Err,
                                                 MachOObjectFile *O,
                                                 ArrayRef<uint8_t> Opcodes,
                                                 bool is64,
                                                 MachOBindEntry::Kind TableKind);

  /// Validate that bind location(s) fall within a mapped section.
  ///
  /// Given a SegIndex, SegOffset, and PointerSize, verify a valid section exists
  /// that fully contains a pointer at that location. Multiple fixups in a bind
  /// (such as with the BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB opcode) can
  /// be tested via the Count and Skip parameters.
  ///
  /// This is used by MachOBindEntry::moveNext() to validate a MachOBindEntry.
  ///
  /// \param SegIndex Segment index to validate.
  /// \param SegOffset Offset within the segment of the first pointer.
  /// \param PointerSize Size in bytes of each pointer.
  /// \param Count Number of pointers in the bind sequence.
  /// \param Skip Bytes to skip between successive pointers.
  /// @return Null on success, or an error message if the location is invalid.
  const char *BindEntryCheckSegAndOffsets(int32_t SegIndex, uint64_t SegOffset,
                                          uint8_t PointerSize,
                                          uint64_t Count = 1,
                                          uint64_t Skip = 0) const {
    return BindRebaseSectionTable->checkSegAndOffsets(SegIndex, SegOffset,
                                                     PointerSize, Count, Skip);
  }

  /// Validate that rebase location(s) fall within a mapped section.
  ///
  /// Given a SegIndex, SegOffset, and PointerSize, verify a valid section exists
  /// that fully contains a pointer at that location. Multiple fixups in a rebase
  /// (such as with the REBASE_OPCODE_DO_*_TIMES* opcodes) can be tested via the
  /// Count and Skip parameters.
  ///
  /// This is used by MachORebaseEntry::moveNext() to validate a MachORebaseEntry.
  ///
  /// \param SegIndex Segment index to validate.
  /// \param SegOffset Offset within the segment of the first pointer.
  /// \param PointerSize Size in bytes of each pointer.
  /// \param Count Number of pointers in the rebase sequence.
  /// \param Skip Bytes to skip between successive pointers.
  /// @return Null on success, or an error message if the location is invalid.
  const char *RebaseEntryCheckSegAndOffsets(int32_t SegIndex,
                                            uint64_t SegOffset,
                                            uint8_t PointerSize,
                                            uint64_t Count = 1,
                                            uint64_t Skip = 0) const {
    return BindRebaseSectionTable->checkSegAndOffsets(SegIndex, SegOffset,
                                                      PointerSize, Count, Skip);
  }

  /// For use with the SegIndex of a checked Mach-O Bind or Rebase entry to
  /// get the segment name.
  /// \param SegIndex Segment index from a validated bind/rebase entry.
  /// @return The segment name for the given segment index.
  StringRef BindRebaseSegmentName(int32_t SegIndex) const {
    return BindRebaseSectionTable->segmentName(SegIndex);
  }

  /// For use with a SegIndex,SegOffset pair from a checked Mach-O Bind or
  /// Rebase entry to get the section name.
  /// \param SegIndex Segment index from a validated bind/rebase entry.
  /// \param SegOffset Offset within the segment.
  /// @return The section name for the given segment index and offset.
  StringRef BindRebaseSectionName(uint32_t SegIndex, uint64_t SegOffset) const {
    return BindRebaseSectionTable->sectionName(SegIndex, SegOffset);
  }

  /// For use with a SegIndex,SegOffset pair from a checked Mach-O Bind or
  /// Rebase entry to get the address.
  /// \param SegIndex Segment index from a validated bind/rebase entry.
  /// \param SegOffset Offset within the segment.
  /// @return The VM address for the given segment index and offset.
  uint64_t BindRebaseAddress(uint32_t SegIndex, uint64_t SegOffset) const {
    return BindRebaseSectionTable->address(SegIndex, SegOffset);
  }

  /// Return the final-linked segment name for section \p Sec.
  ///
  /// In a MachO file, sections have a segment name. This is used in the .o
  /// files. They have a single segment, but this field specifies which segment
  /// a section should be put in the final object.
  ///
  /// \param Sec Opaque section reference.
  /// @return The final-linked segment name for section \p Sec.
  StringRef getSectionFinalSegmentName(DataRefImpl Sec) const;

  /// Return the raw 16-byte section name for \p Sec without C-string trimming.
  ///
  /// Names are stored as 16 bytes. These returns the raw 16 bytes without
  /// interpreting them as a C string.
  ///
  /// \param Sec Opaque section reference.
  /// @return The raw 16-byte section name for \p Sec without C-string trimming.
  ArrayRef<char> getSectionRawName(DataRefImpl Sec) const;
  /// Return the raw 16-byte final segment name for section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The raw 16-byte final segment name for section \p Sec.
  ArrayRef<char> getSectionRawFinalSegmentName(DataRefImpl Sec) const;

  // MachO specific Info about relocations.
  /// Return true if relocation \p RE is a scattered relocation.
  /// \param RE Mach-O relocation info to inspect.
  /// @return True if relocation \p RE is a scattered relocation.
  bool isRelocationScattered(const MachO::any_relocation_info &RE) const;
  /// Return the symbol/section number from plain relocation \p RE.
  /// \param RE Mach-O relocation info to inspect.
  /// @return The symbol/section number from plain relocation \p RE.
  unsigned getPlainRelocationSymbolNum(
                                    const MachO::any_relocation_info &RE) const;
  /// Return true if plain relocation \p RE is external.
  /// \param RE Mach-O relocation info to inspect.
  /// @return True if plain relocation \p RE is external.
  bool getPlainRelocationExternal(const MachO::any_relocation_info &RE) const;
  /// Return the scattered bit from scattered relocation \p RE.
  /// \param RE Mach-O relocation info to inspect.
  /// @return The scattered bit from scattered relocation \p RE.
  bool getScatteredRelocationScattered(
                                    const MachO::any_relocation_info &RE) const;
  /// Return the value field from scattered relocation \p RE.
  /// \param RE Mach-O relocation info to inspect.
  /// @return The value field from scattered relocation \p RE.
  uint32_t getScatteredRelocationValue(
                                    const MachO::any_relocation_info &RE) const;
  /// Return the type field from scattered relocation \p RE.
  /// \param RE Mach-O relocation info to inspect.
  /// @return The type field from scattered relocation \p RE.
  uint32_t getScatteredRelocationType(
                                    const MachO::any_relocation_info &RE) const;
  /// Return the address/r_address field from relocation \p RE.
  /// \param RE Mach-O relocation info to inspect.
  /// @return The address/r_address field from relocation \p RE.
  unsigned getAnyRelocationAddress(const MachO::any_relocation_info &RE) const;
  /// Return the PC-relative bit from relocation \p RE.
  /// \param RE Mach-O relocation info to inspect.
  /// @return The PC-relative bit from relocation \p RE.
  unsigned getAnyRelocationPCRel(const MachO::any_relocation_info &RE) const;
  /// Return the length field from relocation \p RE.
  /// \param RE Mach-O relocation info to inspect.
  /// @return The length field from relocation \p RE.
  unsigned getAnyRelocationLength(const MachO::any_relocation_info &RE) const;
  /// Return the type field from relocation \p RE.
  /// \param RE Mach-O relocation info to inspect.
  /// @return The type field from relocation \p RE.
  unsigned getAnyRelocationType(const MachO::any_relocation_info &RE) const;
  /// Return the section referenced by relocation \p RE, if any.
  /// \param RE Mach-O relocation info to inspect.
  /// @return The section referenced by relocation \p RE, if any.
  SectionRef getAnyRelocationSection(const MachO::any_relocation_info &RE) const;

  // MachO specific structures.
  /// Return the 32-bit section header for opaque section \p DRI.
  /// \param DRI Opaque section reference.
  /// @return The 32-bit section header for opaque section \p DRI.
  MachO::section getSection(DataRefImpl DRI) const;
  /// Return the 64-bit section header for opaque section \p DRI.
  /// \param DRI Opaque section reference.
  /// @return The 64-bit section header for opaque section \p DRI.
  MachO::section_64 getSection64(DataRefImpl DRI) const;
  /// Return 32-bit section header \p Index from segment load command \p L.
  /// \param L Segment load-command info.
  /// \param Index Zero-based section index within the segment.
  /// @return 32-bit section header \p Index from segment load command \p L.
  MachO::section getSection(const LoadCommandInfo &L, unsigned Index) const;
  /// Return 64-bit section header \p Index from segment load command \p L.
  /// \param L Segment64 load-command info.
  /// \param Index Zero-based section index within the segment.
  /// @return 64-bit section header \p Index from segment load command \p L.
  MachO::section_64 getSection64(const LoadCommandInfo &L,unsigned Index) const;
  /// Return the 32-bit nlist entry for opaque symbol \p DRI.
  /// \param DRI Opaque symbol reference.
  /// @return The 32-bit nlist entry for opaque symbol \p DRI.
  MachO::nlist getSymbolTableEntry(DataRefImpl DRI) const;
  /// Return the 64-bit nlist entry for opaque symbol \p DRI.
  /// \param DRI Opaque symbol reference.
  /// @return The 64-bit nlist entry for opaque symbol \p DRI.
  MachO::nlist_64 getSymbol64TableEntry(DataRefImpl DRI) const;

  /// Return the linkedit_data_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The linkedit_data_command at load-command info \p L.
  MachO::linkedit_data_command
  getLinkeditDataLoadCommand(const LoadCommandInfo &L) const;
  /// Return the segment_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The segment_command at load-command info \p L.
  MachO::segment_command
  getSegmentLoadCommand(const LoadCommandInfo &L) const;
  /// Return the segment_command_64 at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The segment_command_64 at load-command info \p L.
  MachO::segment_command_64
  getSegment64LoadCommand(const LoadCommandInfo &L) const;
  /// Return the linker_option_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The linker_option_command at load-command info \p L.
  MachO::linker_option_command
  getLinkerOptionLoadCommand(const LoadCommandInfo &L) const;
  /// Return the version_min_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The version_min_command at load-command info \p L.
  MachO::version_min_command
  getVersionMinLoadCommand(const LoadCommandInfo &L) const;
  /// Return the note_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The note_command at load-command info \p L.
  MachO::note_command
  getNoteLoadCommand(const LoadCommandInfo &L) const;
  /// Return the build_version_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The build_version_command at load-command info \p L.
  MachO::build_version_command
  getBuildVersionLoadCommand(const LoadCommandInfo &L) const;
  /// Return the target_triple_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The target_triple_command at load-command info \p L.
  MachO::target_triple_command
  getTargetTripleLoadCommand(const LoadCommandInfo &L) const;
  /// Return build-tool version entry \p index from LC_BUILD_VERSION.
  /// \param index Zero-based tool-version entry index.
  /// @return Build-tool version entry \p index from LC_BUILD_VERSION.
  MachO::build_tool_version
  getBuildToolVersion(unsigned index) const;
  /// Return the dylib_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The dylib_command at load-command info \p L.
  MachO::dylib_command
  getDylibIDLoadCommand(const LoadCommandInfo &L) const;
  /// Return the dyld_info_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The dyld_info_command at load-command info \p L.
  MachO::dyld_info_command
  getDyldInfoLoadCommand(const LoadCommandInfo &L) const;
  /// Return the dylinker_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The dylinker_command at load-command info \p L.
  MachO::dylinker_command
  getDylinkerCommand(const LoadCommandInfo &L) const;
  /// Return the uuid_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The uuid_command at load-command info \p L.
  MachO::uuid_command
  getUuidCommand(const LoadCommandInfo &L) const;
  /// Return the rpath_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The rpath_command at load-command info \p L.
  MachO::rpath_command
  getRpathCommand(const LoadCommandInfo &L) const;
  /// Return the source_version_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The source_version_command at load-command info \p L.
  MachO::source_version_command
  getSourceVersionCommand(const LoadCommandInfo &L) const;
  /// Return the entry_point_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The entry_point_command at load-command info \p L.
  MachO::entry_point_command
  getEntryPointCommand(const LoadCommandInfo &L) const;
  /// Return the encryption_info_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The encryption_info_command at load-command info \p L.
  MachO::encryption_info_command
  getEncryptionInfoCommand(const LoadCommandInfo &L) const;
  /// Return the encryption_info_command_64 at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The encryption_info_command_64 at load-command info \p L.
  MachO::encryption_info_command_64
  getEncryptionInfoCommand64(const LoadCommandInfo &L) const;
  /// Return the sub_framework_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The sub_framework_command at load-command info \p L.
  MachO::sub_framework_command
  getSubFrameworkCommand(const LoadCommandInfo &L) const;
  /// Return the sub_umbrella_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The sub_umbrella_command at load-command info \p L.
  MachO::sub_umbrella_command
  getSubUmbrellaCommand(const LoadCommandInfo &L) const;
  /// Return the sub_library_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The sub_library_command at load-command info \p L.
  MachO::sub_library_command
  getSubLibraryCommand(const LoadCommandInfo &L) const;
  /// Return the sub_client_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The sub_client_command at load-command info \p L.
  MachO::sub_client_command
  getSubClientCommand(const LoadCommandInfo &L) const;
  /// Return the routines_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The routines_command at load-command info \p L.
  MachO::routines_command
  getRoutinesCommand(const LoadCommandInfo &L) const;
  /// Return the routines_command_64 at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The routines_command_64 at load-command info \p L.
  MachO::routines_command_64
  getRoutinesCommand64(const LoadCommandInfo &L) const;
  /// Return the thread_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The thread_command at load-command info \p L.
  MachO::thread_command
  getThreadCommand(const LoadCommandInfo &L) const;
  /// Return the fileset_entry_command at load-command info \p L.
  /// \param L Load-command info whose bytes describe the command.
  /// @return The fileset_entry_command at load-command info \p L.
  MachO::fileset_entry_command
  getFilesetEntryLoadCommand(const LoadCommandInfo &L) const;

  /// Return the any_relocation_info for opaque relocation \p Rel.
  /// \param Rel Opaque relocation reference.
  /// @return The any_relocation_info for opaque relocation \p Rel.
  MachO::any_relocation_info getRelocation(DataRefImpl Rel) const;
  /// Return the data_in_code_entry for opaque dice reference \p Rel.
  /// \param Rel Opaque data-in-code reference.
  /// @return The data_in_code_entry for opaque dice reference \p Rel.
  MachO::data_in_code_entry getDice(DataRefImpl Rel) const;
  /// Return the 32-bit Mach-O header for this object.
  /// @return The 32-bit Mach-O header for this object.
  const MachO::mach_header &getHeader() const;
  /// Return the 64-bit Mach-O header for this object.
  /// @return The 64-bit Mach-O header for this object.
  const MachO::mach_header_64 &getHeader64() const;
  /// Return indirect-symbol table entry \p Index from dysymtab \p DLC.
  /// \param DLC Dysymtab load command describing the table.
  /// \param Index Zero-based entry index.
  /// @return Indirect-symbol table entry \p Index from dysymtab \p DLC.
  uint32_t
  getIndirectSymbolTableEntry(const MachO::dysymtab_command &DLC,
                              unsigned Index) const;
  /// Return data-in-code table entry \p Index at file offset \p DataOffset.
  /// \param DataOffset File offset of the data-in-code table.
  /// \param Index Zero-based entry index.
  /// @return Data-in-code table entry \p Index at file offset \p DataOffset.
  MachO::data_in_code_entry getDataInCodeTableEntry(uint32_t DataOffset,
                                                    unsigned Index) const;
  /// Return the LC_SYMTAB load command.
  /// @return The LC_SYMTAB load command.
  MachO::symtab_command getSymtabLoadCommand() const;
  /// Return the LC_DYSYMTAB load command.
  /// @return The LC_DYSYMTAB load command.
  MachO::dysymtab_command getDysymtabLoadCommand() const;
  /// Return the LC_DATA_IN_CODE linkedit data command.
  /// @return The LC_DATA_IN_CODE linkedit data command.
  MachO::linkedit_data_command getDataInCodeLoadCommand() const;
  /// Return the LC_LINKER_OPTIMIZATION_HINT linkedit data command.
  /// @return The LC_LINKER_OPTIMIZATION_HINT linkedit data command.
  MachO::linkedit_data_command getLinkOptHintsLoadCommand() const;
  /// Return the dyld rebase opcode bytes from LC_DYLD_INFO.
  /// @return The dyld rebase opcode bytes from LC_DYLD_INFO.
  ArrayRef<uint8_t> getDyldInfoRebaseOpcodes() const;
  /// Return the dyld bind opcode bytes from LC_DYLD_INFO.
  /// @return The dyld bind opcode bytes from LC_DYLD_INFO.
  ArrayRef<uint8_t> getDyldInfoBindOpcodes() const;
  /// Return the dyld weak-bind opcode bytes from LC_DYLD_INFO.
  /// @return The dyld weak-bind opcode bytes from LC_DYLD_INFO.
  ArrayRef<uint8_t> getDyldInfoWeakBindOpcodes() const;
  /// Return the dyld lazy-bind opcode bytes from LC_DYLD_INFO.
  /// @return The dyld lazy-bind opcode bytes from LC_DYLD_INFO.
  ArrayRef<uint8_t> getDyldInfoLazyBindOpcodes() const;
  /// Return the dyld exports-trie bytes from LC_DYLD_INFO.
  /// @return The dyld exports-trie bytes from LC_DYLD_INFO.
  ArrayRef<uint8_t> getDyldInfoExportsTrie() const;

  /// If the optional is std::nullopt, no header was found, but the object was
  /// well-formed.
  /// @return The chained fixups header if present, nullopt if absent, or an error.
  Expected<std::optional<MachO::dyld_chained_fixups_header>>
  getChainedFixupsHeader() const;
  /// Return the chained-fixup import targets from this object.
  /// @return The chained-fixup import targets, or an error.
  Expected<std::vector<ChainedFixupTarget>> getDyldChainedFixupTargets() const;

  /// Return the LC_DYLD_CHAINED_FIXUPS load command, if present.
  ///
  /// Note: This is a limited, temporary API, which will be removed when Apple
  /// upstreams their implementation. Please do not rely on this.
  /// @return The chained fixups load command if present, nullopt if absent, or an error.
  Expected<std::optional<MachO::linkedit_data_command>>
  getChainedFixupsLoadCommand() const;
  /// Return chained-fixup segment metadata for this object.
  ///
  /// Returns the number of sections listed in dyld_chained_starts_in_image, and
  /// a ChainedFixupsSegment for each segment that has fixups.
  /// @return The number of sections and a ChainedFixupsSegment for each segment with fixups.
  Expected<std::pair<size_t, std::vector<ChainedFixupsSegment>>>
  getChainedFixupsSegments() const;
  /// Return the LC_DYLD_EXPORTS_TRIE payload bytes, if present.
  /// @return The LC_DYLD_EXPORTS_TRIE payload bytes, if present.
  ArrayRef<uint8_t> getDyldExportsTrie() const;

  /// Return function-start addresses from LC_FUNCTION_STARTS.
  /// @return Function-start addresses from LC_FUNCTION_STARTS.
  SmallVector<uint64_t> getFunctionStarts() const;
  /// Return the UUID bytes from LC_UUID, if present.
  /// @return The UUID bytes from LC_UUID, if present.
  ArrayRef<uint8_t> getUuid() const;

  /// Return the contents of the symbol string table.
  /// @return The contents of the symbol string table.
  StringRef getStringTableData() const;

  /// Decode a ULEB128 sequence starting at string-table index \p Index.
  /// \param Index Offset into the string table where the ULEB128s begin.
  /// \param Out Receives the decoded ULEB128 values.
  void ReadULEB128s(uint64_t Index, SmallVectorImpl<uint64_t> &Out) const;

  /// Guess a short library name and whether \p Name names a framework.
  /// \param Name Full install or path name to analyze.
  /// \param isFramework Set to true if \p Name looks like a framework path.
  /// \param Suffix Set to any recognized version/suffix substring.
  /// @return A guessed short library name derived from \p Name.
  static StringRef guessLibraryShortName(StringRef Name, bool &isFramework,
                                         StringRef &Suffix);

  /// Return the Triple::ArchType for Mach-O CPU type/subtype.
  /// \param CPUType Mach-O CPU type.
  /// \param CPUSubType Mach-O CPU subtype.
  /// @return The Triple::ArchType for Mach-O CPU type/subtype.
  static Triple::ArchType getArch(uint32_t CPUType, uint32_t CPUSubType);
  /// Return an architecture Triple for Mach-O CPU type/subtype.
  /// \param CPUType Mach-O CPU type.
  /// \param CPUSubType Mach-O CPU subtype.
  /// \param McpuDefault Optional output for a default -mcpu string.
  /// \param ArchFlag Optional output for a macho arch flag string.
  /// @return An architecture Triple for Mach-O CPU type/subtype.
  static Triple getArchTriple(uint32_t CPUType, uint32_t CPUSubType,
                              const char **McpuDefault = nullptr,
                              const char **ArchFlag = nullptr);
  /// Return true if \p ArchFlag is a recognized Mach-O architecture flag.
  /// \param ArchFlag Architecture flag string to validate.
  /// @return True if \p ArchFlag is a recognized Mach-O architecture flag.
  static bool isValidArch(StringRef ArchFlag);
  /// Return the list of recognized Mach-O architecture flag strings.
  /// @return The list of recognized Mach-O architecture flag strings.
  static ArrayRef<StringRef> getValidArchs();
  /// Return a Triple describing the host architecture.
  /// @return A Triple describing the host architecture.
  static Triple getHostArch();

  /// Return true if this is a relocatable Mach-O object (MH_OBJECT).
  /// @return True if this is a relocatable Mach-O object (MH_OBJECT).
  bool isRelocatableObject() const override;

  /// Map a DWARF-style debug section name to its Mach-O counterpart.
  /// \param Name Canonical debug section name to map.
  /// @return The Mach-O counterpart of the DWARF-style debug section name.
  StringRef mapDebugSectionName(StringRef Name) const override;

  /// Map a Swift reflection section name to its section-kind enum value.
  /// \param SectionName Swift reflection section name.
  /// @return The Swift reflection section-kind enum value for \p SectionName.
  llvm::binaryformat::Swift5ReflectionSectionKind
  mapReflectionSectionNameToEnumValue(StringRef SectionName) const override;

  /// Return true if this object has a __PAGEZERO segment.
  /// @return True if this object has a __PAGEZERO segment.
  bool hasPageZeroSegment() const { return HasPageZeroSegment; }

  /// Return the fileset entry offset used when this slice was opened.
  /// @return The fileset entry offset used when this slice was opened.
  size_t getMachOFilesetEntryOffset() const { return MachOFilesetEntryOffset; }

  /// Return true if \p v is a Mach-O object file.
  /// \param v Binary to test.
  /// @return True if \p v is a Mach-O object file.
  static bool classof(const Binary *v) {
    return v->isMachO();
  }

  /// Return the major component of a version_min command version or SDK.
  /// \param C Version-min load command.
  /// \param SDK If true, decode \p C.sdk; otherwise decode \p C.version.
  /// @return The major component of a version_min command version or SDK.
  static uint32_t
  getVersionMinMajor(MachO::version_min_command &C, bool SDK) {
    uint32_t VersionOrSDK = (SDK) ? C.sdk : C.version;
    return (VersionOrSDK >> 16) & 0xffff;
  }

  /// Return the minor component of a version_min command version or SDK.
  /// \param C Version-min load command.
  /// \param SDK If true, decode \p C.sdk; otherwise decode \p C.version.
  /// @return The minor component of a version_min command version or SDK.
  static uint32_t
  getVersionMinMinor(MachO::version_min_command &C, bool SDK) {
    uint32_t VersionOrSDK = (SDK) ? C.sdk : C.version;
    return (VersionOrSDK >> 8) & 0xff;
  }

  /// Return the update/patch component of a version_min command version or SDK.
  /// \param C Version-min load command.
  /// \param SDK If true, decode \p C.sdk; otherwise decode \p C.version.
  /// @return The update/patch component of a version_min command version or SDK.
  static uint32_t
  getVersionMinUpdate(MachO::version_min_command &C, bool SDK) {
    uint32_t VersionOrSDK = (SDK) ? C.sdk : C.version;
    return VersionOrSDK & 0xff;
  }

  /// Return a display name for build-version platform \p platform.
  /// \param platform Mach-O PLATFORM_* identifier.
  /// @return A display name for build-version platform \p platform.
  static std::string getBuildPlatform(uint32_t platform) {
    switch (platform) {
#define PLATFORM(platform, id, name, build_name, target, tapi_target,          \
                 marketing)                                                    \
  case MachO::PLATFORM_##platform:                                             \
    return #name;
#include "llvm/BinaryFormat/MachO.def"
    default:
      std::string ret;
      raw_string_ostream ss(ret);
      ss << format_hex(platform, 8, true);
      return ret;
    }
  }

  /// Return a display name for build-version tool \p tools.
  /// \param tools Mach-O TOOL_* identifier.
  /// @return A display name for build-version tool \p tools.
  static std::string getBuildTool(uint32_t tools) {
    switch (tools) {
    case MachO::TOOL_CLANG: return "clang";
    case MachO::TOOL_SWIFT: return "swift";
    case MachO::TOOL_LD: return "ld";
    case MachO::TOOL_LLD:
      return "lld";
    default:
      std::string ret;
      raw_string_ostream ss(ret);
      ss << format_hex(tools, 8, true);
      return ret;
    }
  }

  /// Format a packed Mach-O version word as a dotted string.
  /// \param version Packed major/minor/update version word.
  /// @return The version formatted as a dotted string.
  static std::string getVersionString(uint32_t version) {
    uint32_t major = (version >> 16) & 0xffff;
    uint32_t minor = (version >> 8) & 0xff;
    uint32_t update = version & 0xff;

    SmallString<32> Version;
    Version = utostr(major) + "." + utostr(minor);
    if (update != 0)
      Version += "." + utostr(update);
    return std::string(std::string(Version));
  }

  /// Return object-file paths inside a .dSYM bundle, or an empty vector.
  ///
  /// If the input path is a .dSYM bundle (as created by the dsymutil tool),
  /// return the paths to the object files found in the bundle, otherwise return
  /// an empty vector. If the path appears to be a .dSYM bundle but no objects
  /// were found or there was a filesystem error, then return an error.
  ///
  /// \param Path File-system path that may be a .dSYM bundle.
  /// @return Object-file paths inside the .dSYM bundle, an empty vector, or an error.
  static Expected<std::vector<std::string>>
  findDsymObjectMembers(StringRef Path);

private:
  MachOObjectFile(MemoryBufferRef Object, bool IsLittleEndian, bool Is64Bits,
                  Error &Err, uint32_t UniversalCputype = 0,
                  uint32_t UniversalIndex = 0,
                  size_t MachOFilesetEntryOffset = 0);

  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;

  union {
    MachO::mach_header_64 Header64; ///< 64-bit Mach-O header when is64Bit().
    MachO::mach_header Header;      ///< 32-bit Mach-O header when !is64Bit().
  };
  using SectionList = SmallVector<const char*, 1>;
  SectionList Sections;
  using LibraryList = SmallVector<const char*, 1>;
  LibraryList Libraries;
  LoadCommandList LoadCommands;
  using LibraryShortName = SmallVector<StringRef, 1>;
  using BuildToolList = SmallVector<const char*, 1>;
  BuildToolList BuildTools;
  mutable LibraryShortName LibrariesShortNames;
  std::unique_ptr<BindRebaseSegInfo> BindRebaseSectionTable;
  const char *SymtabLoadCmd = nullptr;
  const char *DysymtabLoadCmd = nullptr;
  const char *DataInCodeLoadCmd = nullptr;
  const char *LinkOptHintsLoadCmd = nullptr;
  const char *DyldInfoLoadCmd = nullptr;
  const char *FuncStartsLoadCmd = nullptr;
  const char *DyldChainedFixupsLoadCmd = nullptr;
  const char *DyldExportsTrieLoadCmd = nullptr;
  const char *UuidLoadCmd = nullptr;
  bool HasPageZeroSegment = false;
  size_t MachOFilesetEntryOffset = 0;
};

inline DiceRef::DiceRef(DataRefImpl DiceP, const ObjectFile *Owner)
  : DicePimpl(DiceP) , OwningObject(Owner) {}

inline bool DiceRef::operator==(const DiceRef &Other) const {
  return DicePimpl == Other.DicePimpl;
}

inline bool DiceRef::operator<(const DiceRef &Other) const {
  return DicePimpl < Other.DicePimpl;
}

inline void DiceRef::moveNext() {
  const MachO::data_in_code_entry *P =
    reinterpret_cast<const MachO::data_in_code_entry *>(DicePimpl.p);
  DicePimpl.p = reinterpret_cast<uintptr_t>(P + 1);
}

// Since a Mach-O data in code reference, a DiceRef, can only be created when
// the OwningObject ObjectFile is a MachOObjectFile a static_cast<> is used for
// the methods that get the values of the fields of the reference.

inline std::error_code DiceRef::getOffset(uint32_t &Result) const {
  const MachOObjectFile *MachOOF =
    static_cast<const MachOObjectFile *>(OwningObject);
  MachO::data_in_code_entry Dice = MachOOF->getDice(DicePimpl);
  Result = Dice.offset;
  return std::error_code();
}

inline std::error_code DiceRef::getLength(uint16_t &Result) const {
  const MachOObjectFile *MachOOF =
    static_cast<const MachOObjectFile *>(OwningObject);
  MachO::data_in_code_entry Dice = MachOOF->getDice(DicePimpl);
  Result = Dice.length;
  return std::error_code();
}

inline std::error_code DiceRef::getKind(uint16_t &Result) const {
  const MachOObjectFile *MachOOF =
    static_cast<const MachOObjectFile *>(OwningObject);
  MachO::data_in_code_entry Dice = MachOOF->getDice(DicePimpl);
  Result = Dice.kind;
  return std::error_code();
}

inline DataRefImpl DiceRef::getRawDataRefImpl() const {
  return DicePimpl;
}

inline const ObjectFile *DiceRef::getObjectFile() const {
  return OwningObject;
}

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_MACHO_H
