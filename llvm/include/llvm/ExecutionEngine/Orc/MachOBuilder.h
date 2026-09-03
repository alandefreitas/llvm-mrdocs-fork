//===------------ MachOBuilder.h -- Build MachO Objects ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Build MachO object files for interaction with the ObjC runtime and debugger.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MACHOBUILDER_H
#define LLVM_EXECUTIONENGINE_ORC_MACHOBUILDER_H

#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"

#include <list>
#include <map>
#include <vector>

namespace llvm {
namespace orc {

/// Write a MachO structure into a buffer at the given offset.
/// @param Buf Destination buffer for the serialized structure.
/// @param Offset Byte offset within \p Buf at which to write.
/// @param S Structure value to serialize.
/// @param SwapStruct Whether to byte-swap \p S before writing.
/// @return The offset immediately after the written structure.
template <typename MachOStruct>
size_t writeMachOStruct(MutableArrayRef<char> Buf, size_t Offset, MachOStruct S,
                        bool SwapStruct) {
  if (SwapStruct)
    MachO::swapStruct(S);
  assert(Offset + sizeof(MachOStruct) <= Buf.size() && "Buffer overflow");
  memcpy(&Buf[Offset], reinterpret_cast<const char *>(&S), sizeof(MachOStruct));
  return Offset + sizeof(MachOStruct);
}

/// Base type for MachOBuilder load command wrappers.
struct MachOBuilderLoadCommandBase {
  /// Destroy a MachOBuilder load command wrapper.
  virtual ~MachOBuilderLoadCommandBase() = default;
  /// Return the size in bytes of this load command.
  /// @return The size in bytes of this load command.
  virtual size_t size() const = 0;
  /// Write this load command into \p Buf at \p Offset.
  /// @param Buf Destination buffer for the serialized load command.
  /// @param Offset Byte offset within \p Buf at which to write.
  /// @param SwapStruct Whether to byte-swap multi-byte fields before writing.
  /// @return The offset immediately after the written load command.
  virtual size_t write(MutableArrayRef<char> Buf, size_t Offset,
                       bool SwapStruct) = 0;
};

/// MachOBuilder load command wrapper type.
template <MachO::LoadCommandType LCType> struct MachOBuilderLoadCommandImplBase;

#define HANDLE_LOAD_COMMAND(Name, Value, LCStruct)                             \
  template <>                                                                  \
  struct MachOBuilderLoadCommandImplBase<MachO::Name>                          \
      : public MachO::LCStruct, public MachOBuilderLoadCommandBase {           \
    using CmdStruct = LCStruct;                                                \
    MachOBuilderLoadCommandImplBase() {                                        \
      memset(&rawStruct(), 0, sizeof(CmdStruct));                              \
      cmd = Value;                                                             \
      cmdsize = sizeof(CmdStruct);                                             \
    }                                                                          \
    template <typename... ArgTs>                                               \
    MachOBuilderLoadCommandImplBase(ArgTs &&...Args)                           \
        : CmdStruct{Value, sizeof(CmdStruct), std::forward<ArgTs>(Args)...} {} \
    CmdStruct &rawStruct() { return static_cast<CmdStruct &>(*this); }         \
    size_t size() const override { return cmdsize; }                           \
    size_t write(MutableArrayRef<char> Buf, size_t Offset,                     \
                 bool SwapStruct) override {                                   \
      return writeMachOStruct(Buf, Offset, rawStruct(), SwapStruct);           \
    }                                                                          \
  };

#include "llvm/BinaryFormat/MachO.def"

#undef HANDLE_LOAD_COMMAND

/// MachOBuilder wrapper for a specific load command type.
template <MachO::LoadCommandType LCType>
struct MachOBuilderLoadCommand
    : public MachOBuilderLoadCommandImplBase<LCType> {
public:
  /// Construct a default-initialized load command of type \p LCType.
  MachOBuilderLoadCommand() = default;

  /// Construct a load command of type \p LCType from the given arguments.
  /// @param Args Arguments forwarded to the underlying MachO command struct.
  template <typename... ArgTs>
  MachOBuilderLoadCommand(ArgTs &&...Args)
      : MachOBuilderLoadCommandImplBase<LCType>(std::forward<ArgTs>(Args)...) {}
};

/// MachOBuilder wrapper for an LC_UUID load command.
template <>
struct MachOBuilderLoadCommand<MachO::LC_UUID>
    : public MachOBuilderLoadCommandImplBase<MachO::LC_UUID> {
  /// Construct an LC_UUID command from a 16-byte UUID array.
  /// @param UUID 16-byte UUID value to store in the command.
  MachOBuilderLoadCommand(const uint8_t (&UUID)[16])
      : MachOBuilderLoadCommandImplBase<MachO::LC_UUID>() {
    memcpy(uuid, UUID, sizeof(uuid));
  }

  /// Construct an LC_UUID command from a std::array UUID.
  /// @param UUID 16-byte UUID value to store in the command.
  MachOBuilderLoadCommand(const std::array<uint8_t, 16> &UUID)
      : MachOBuilderLoadCommandImplBase<MachO::LC_UUID>() {
    memcpy(uuid, UUID.data(), sizeof(uuid));
  }
};

/// MachOBuilder wrapper for dylib load commands that carry a path name.
template <MachO::LoadCommandType LCType>
struct MachOBuilderDylibLoadCommand
    : public MachOBuilderLoadCommandImplBase<LCType> {

  /// Construct a dylib load command with the given name and version fields.
  /// @param Name Install name / path stored after the command header.
  /// @param Timestamp Dylib timestamp field.
  /// @param CurrentVersion Dylib current version field.
  /// @param CompatibilityVersion Dylib compatibility version field.
  MachOBuilderDylibLoadCommand(std::string Name, uint32_t Timestamp,
                               uint32_t CurrentVersion,
                               uint32_t CompatibilityVersion)
      : MachOBuilderLoadCommandImplBase<LCType>(
            MachO::dylib{24, Timestamp, CurrentVersion, CompatibilityVersion}),
        Name(std::move(Name)) {
    this->cmdsize += (this->Name.size() + 1 + 3) & ~0x3;
  }

  /// Write this dylib load command and its trailing name string.
  /// @param Buf Destination buffer for the serialized load command.
  /// @param Offset Byte offset within \p Buf at which to write.
  /// @param SwapStruct Whether to byte-swap multi-byte fields before writing.
  /// @return The offset immediately after the written command and name.
  size_t write(MutableArrayRef<char> Buf, size_t Offset,
               bool SwapStruct) override {
    Offset = writeMachOStruct(Buf, Offset, this->rawStruct(), SwapStruct);
    strcpy(Buf.data() + Offset, Name.data());
    return Offset + ((Name.size() + 1 + 3) & ~0x3);
  }

  /// Dylib install name / path stored after the command header.
  std::string Name;
};

/// MachOBuilder wrapper for an LC_ID_DYLIB load command.
template <>
struct MachOBuilderLoadCommand<MachO::LC_ID_DYLIB>
    : public MachOBuilderDylibLoadCommand<MachO::LC_ID_DYLIB> {
  /// Inherit the dylib load command constructors.
  using MachOBuilderDylibLoadCommand::MachOBuilderDylibLoadCommand;
};

/// MachOBuilder wrapper for an LC_LOAD_DYLIB load command.
template <>
struct MachOBuilderLoadCommand<MachO::LC_LOAD_DYLIB>
    : public MachOBuilderDylibLoadCommand<MachO::LC_LOAD_DYLIB> {
  /// Inherit the dylib load command constructors.
  using MachOBuilderDylibLoadCommand::MachOBuilderDylibLoadCommand;
};

/// MachOBuilder wrapper for an LC_LOAD_WEAK_DYLIB load command.
template <>
struct MachOBuilderLoadCommand<MachO::LC_LOAD_WEAK_DYLIB>
    : public MachOBuilderDylibLoadCommand<MachO::LC_LOAD_WEAK_DYLIB> {
  /// Inherit the dylib load command constructors.
  using MachOBuilderDylibLoadCommand::MachOBuilderDylibLoadCommand;
};

/// MachOBuilder wrapper for an LC_RPATH load command.
template <>
struct MachOBuilderLoadCommand<MachO::LC_RPATH>
    : public MachOBuilderLoadCommandImplBase<MachO::LC_RPATH> {
  /// Construct an LC_RPATH command with the given runtime search path.
  /// @param Path Runtime search path stored after the command header.
  MachOBuilderLoadCommand(std::string Path)
      : MachOBuilderLoadCommandImplBase(12u), Path(std::move(Path)) {
    cmdsize += (this->Path.size() + 1 + 3) & ~0x3;
  }

  /// Write this LC_RPATH command and its trailing path string.
  /// @param Buf Destination buffer for the serialized load command.
  /// @param Offset Byte offset within \p Buf at which to write.
  /// @param SwapStruct Whether to byte-swap multi-byte fields before writing.
  /// @return The offset immediately after the written command and path.
  size_t write(MutableArrayRef<char> Buf, size_t Offset,
               bool SwapStruct) override {
    Offset = writeMachOStruct(Buf, Offset, rawStruct(), SwapStruct);
    strcpy(Buf.data() + Offset, Path.data());
    return Offset + ((Path.size() + 1 + 3) & ~0x3);
  }

  /// Runtime search path stored after the command header.
  std::string Path;
};

/// MachOBuilder wrapper for an LC_TARGET_TRIPLE load command.
template <>
struct MachOBuilderLoadCommand<MachO::LC_TARGET_TRIPLE>
    : public MachOBuilderLoadCommandImplBase<MachO::LC_TARGET_TRIPLE> {
  /// Construct an LC_TARGET_TRIPLE command with the given triple string.
  /// @param TargetTriple Target triple stored after the command header.
  MachOBuilderLoadCommand(std::string TargetTriple)
      : MachOBuilderLoadCommandImplBase(12u),
        TargetTriple(std::move(TargetTriple)) {
    cmdsize += (this->TargetTriple.size() + 1 + 3) & ~0x3;
  }

  /// Write this LC_TARGET_TRIPLE command and its trailing triple string.
  /// @param Buf Destination buffer for the serialized load command.
  /// @param Offset Byte offset within \p Buf at which to write.
  /// @param SwapStruct Whether to byte-swap multi-byte fields before writing.
  /// @return The offset immediately after the written command and triple.
  size_t write(MutableArrayRef<char> Buf, size_t Offset,
               bool SwapStruct) override {
    Offset = writeMachOStruct(Buf, Offset, rawStruct(), SwapStruct);
    strcpy(Buf.data() + Offset, TargetTriple.data());
    return Offset + ((TargetTriple.size() + 1 + 3) & ~0x3);
  }

  /// Target triple string stored after the command header.
  std::string TargetTriple;
};

/// Builder for in-memory MachO object files.
template <typename MachOTraits> class MachOBuilder {
private:
  struct SymbolContainer {
    size_t SymbolIndexBase = 0;
    std::vector<typename MachOTraits::NList> Symbols;
  };

  struct StringTableEntry {
    StringRef S;
    size_t Offset;
  };

  using StringTable = std::vector<StringTableEntry>;

  static bool swapStruct() {
    return MachOTraits::Endianness != llvm::endianness::native;
  }

public:
  /// Identifier for a string in the builder's string table.
  using StringId = size_t;

  struct Section;

  /// Relocation target referring to either a symbol or a section.
  ///
  /// Points to either an nlist entry (as a (symbol-container, index) pair), or
  /// a section.
  class RelocTarget {
  public:
    /// Construct a relocation target that refers to a section.
    /// @param S Section that is the relocation target.
    RelocTarget(const Section &S) : S(&S), Idx(~0U) {}
    /// Construct a relocation target that refers to a symbol.
    /// @param SC Symbol container holding the target symbol.
    /// @param Idx Index of the symbol within \p SC.
    RelocTarget(SymbolContainer &SC, size_t Idx) : SC(&SC), Idx(Idx) {}

    /// Return true if this target refers to a symbol rather than a section.
    /// @return True if this target refers to a symbol.
    bool isSymbol() { return Idx != ~0U; }

    /// Return the symbol number for a symbol relocation target.
    /// @return The symbol number for this symbol target.
    uint32_t getSymbolNum() {
      assert(isSymbol() && "Target is not a symbol");
      return SC->SymbolIndexBase + Idx;
    }

    /// Return the section number for a section relocation target.
    /// @return The section number for this section target.
    uint32_t getSectionId() {
      assert(!isSymbol() && "Target is not a section");
      return S->SectionNumber;
    }

    /// Return a mutable reference to the targeted nlist entry.
    /// @return A mutable reference to the targeted nlist entry.
    typename MachOTraits::NList &nlist() {
      assert(isSymbol() && "Target is not a symbol");
      return SC->Symbols[Idx];
    }

  private:
    union {
      /// Section pointed to when this target is section-based.
      const Section *S;
      /// Symbol container pointed to when this target is symbol-based.
      SymbolContainer *SC;
    };
    size_t Idx;
  };

  /// Relocation entry with an associated reloc target.
  struct Reloc : public MachO::relocation_info {
    /// Symbol or section that this relocation refers to.
    RelocTarget Target;

    /// Construct a relocation at \p Offset targeting \p Target.
    /// @param Offset Offset within the section content to relocate.
    /// @param Target Symbol or section that the relocation refers to.
    /// @param PCRel Whether the relocation is PC-relative.
    /// @param Length Encoded operand length (as in MachO r_length).
    /// @param Type MachO relocation type code.
    Reloc(int32_t Offset, RelocTarget Target, bool PCRel, unsigned Length,
          unsigned Type)
        : Target(Target) {
      assert(Type < 16 && "Relocation type out of range");
      r_address = Offset; // Will slide to account for sec addr during layout
      r_symbolnum = 0;
      r_pcrel = PCRel;
      r_length = Length;
      r_extern = Target.isSymbol();
      r_type = Type;
    }

    /// Return a reference to the underlying MachO relocation_info.
    /// @return A reference to the underlying MachO relocation_info.
    MachO::relocation_info &rawStruct() {
      return static_cast<MachO::relocation_info &>(*this);
    }
  };

  /// Optional raw bytes that back a section's file content.
  struct SectionContent {
    /// Pointer to the section's content bytes, or null for zero-fill.
    const char *Data = nullptr;
    /// Size in bytes of the section content.
    size_t Size = 0;
  };

  /// Section within a MachOBuilder segment.
  struct Section : public MachOTraits::Section, public RelocTarget {
    /// Builder that owns this section.
    MachOBuilder &Builder;
    /// Optional content bytes for this section.
    SectionContent Content;
    /// 1-based section number assigned during layout.
    size_t SectionNumber = 0;
    /// Symbols defined relative to this section.
    SymbolContainer SC;
    /// Relocations applying to this section's content.
    std::vector<Reloc> Relocs;

    /// Construct a section with the given section and segment names.
    /// @param Builder Builder that owns this section.
    /// @param SecName Section name (at most 16 bytes).
    /// @param SegName Segment name (at most 16 bytes).
    Section(MachOBuilder &Builder, StringRef SecName, StringRef SegName)
        : RelocTarget(*this), Builder(Builder) {
      memset(&rawStruct(), 0, sizeof(typename MachOTraits::Section));
      assert(SecName.size() <= 16 && "SecName too long");
      assert(SegName.size() <= 16 && "SegName too long");
      memcpy(this->sectname, SecName.data(), SecName.size());
      memcpy(this->segname, SegName.data(), SegName.size());
    }

    /// Add a symbol defined in this section at the given content offset.
    /// @param Offset Offset within this section's content.
    /// @param Name Symbol name to add to the string table.
    /// @param Type MachO n_type bits (N_SECT is applied automatically).
    /// @param Desc MachO n_desc value.
    /// @return A reloc target referring to the newly added symbol.
    RelocTarget addSymbol(int32_t Offset, StringRef Name, uint8_t Type,
                          uint16_t Desc) {
      StringId SI = Builder.addString(Name);
      typename MachOTraits::NList Sym;
      Sym.n_strx = SI;
      Sym.n_type = Type | MachO::N_SECT;
      Sym.n_sect = MachO::NO_SECT; // Will be filled in later.
      Sym.n_desc = Desc;
      Sym.n_value = Offset;
      SC.Symbols.push_back(Sym);
      return {SC, SC.Symbols.size() - 1};
    }

    /// Add a relocation in this section at the given content offset.
    /// @param Offset Offset within this section's content to relocate.
    /// @param Target Symbol or section that the relocation refers to.
    /// @param PCRel Whether the relocation is PC-relative.
    /// @param Length Encoded operand length (as in MachO r_length).
    /// @param Type MachO relocation type code.
    void addReloc(int32_t Offset, RelocTarget Target, bool PCRel,
                  unsigned Length, unsigned Type) {
      Relocs.push_back({Offset, Target, PCRel, Length, Type});
    }

    /// Return a reference to the underlying MachO section struct.
    /// @return A reference to the underlying MachO section struct.
    auto &rawStruct() {
      return static_cast<typename MachOTraits::Section &>(*this);
    }
  };

  /// Segment load command that owns zero or more sections.
  struct Segment : public MachOBuilderLoadCommand<MachOTraits::SegmentCmd> {
    /// Builder that owns this segment.
    MachOBuilder &Builder;
    /// Sections contained in this segment.
    std::vector<std::unique_ptr<Section>> Sections;

    /// Construct a segment with the given name and default protections.
    /// @param Builder Builder that owns this segment.
    /// @param SegName Segment name (at most 16 bytes).
    Segment(MachOBuilder &Builder, StringRef SegName)
        : MachOBuilderLoadCommand<MachOTraits::SegmentCmd>(), Builder(Builder) {
      assert(SegName.size() <= 16 && "SegName too long");
      memcpy(this->segname, SegName.data(), SegName.size());
      this->maxprot =
          MachO::VM_PROT_READ | MachO::VM_PROT_WRITE | MachO::VM_PROT_EXECUTE;
      this->initprot = this->maxprot;
    }

    /// Add a section with the given section and segment names.
    /// @param SecName Section name (at most 16 bytes).
    /// @param SegName Segment name stored in the section header.
    /// @return A reference to the newly added section.
    Section &addSection(StringRef SecName, StringRef SegName) {
      Sections.push_back(std::make_unique<Section>(Builder, SecName, SegName));
      return *Sections.back();
    }

    /// Write this segment load command and its section headers.
    /// @param Buf Destination buffer for the serialized segment.
    /// @param Offset Byte offset within \p Buf at which to write.
    /// @param SwapStruct Whether to byte-swap multi-byte fields before writing.
    /// @return The offset immediately after the written segment and sections.
    size_t write(MutableArrayRef<char> Buf, size_t Offset,
                 bool SwapStruct) override {
      Offset = MachOBuilderLoadCommand<MachOTraits::SegmentCmd>::write(
          Buf, Offset, SwapStruct);
      for (auto &Sec : Sections)
        Offset = writeMachOStruct(Buf, Offset, Sec->rawStruct(), SwapStruct);
      return Offset;
    }
  };

  /// Construct a MachOBuilder using the given page size for layout.
  /// @param PageSize Page size used when aligning non-object segment VM sizes.
  MachOBuilder(size_t PageSize) : PageSize(PageSize) {
    memset((char *)&Header, 0, sizeof(Header));
    Header.magic = MachOTraits::Magic;
  }

  /// Add a non-segment load command of type \p LCType.
  /// @param Args Arguments forwarded to the load command constructor.
  /// @return A reference to the newly added load command.
  template <MachO::LoadCommandType LCType, typename... ArgTs>
  MachOBuilderLoadCommand<LCType> &addLoadCommand(ArgTs &&...Args) {
    static_assert(LCType != MachOTraits::SegmentCmd,
                  "Use addSegment to add segment load command");
    auto LC = std::make_unique<MachOBuilderLoadCommand<LCType>>(
        std::forward<ArgTs>(Args)...);
    auto &Tmp = *LC;
    LoadCommands.push_back(std::move(LC));
    return Tmp;
  }

  /// Add \p Str to the string table and return its string id.
  /// @param Str String to intern in the builder's string table.
  /// @return Identifier for \p Str in the string table.
  StringId addString(StringRef Str) {
    if (Strings.empty() && !Str.empty())
      addString("");
    return Strings.insert(std::make_pair(Str, Strings.size())).first->second;
  }

  /// Add a segment with the given name.
  /// @param SegName Segment name (at most 16 bytes).
  /// @return A reference to the newly added segment.
  Segment &addSegment(StringRef SegName) {
    Segments.push_back(Segment(*this, SegName));
    return Segments.back();
  }

  /// Add a non-section symbol to the object.
  /// @param Name Symbol name to add to the string table.
  /// @param Type MachO n_type value.
  /// @param Sect MachO n_sect value.
  /// @param Desc MachO n_desc value.
  /// @param Value MachO n_value value.
  /// @return A reloc target referring to the newly added symbol.
  RelocTarget addSymbol(StringRef Name, uint8_t Type, uint8_t Sect,
                        uint16_t Desc, typename MachOTraits::UIntPtr Value) {
    StringId SI = addString(Name);
    typename MachOTraits::NList Sym;
    Sym.n_strx = SI;
    Sym.n_type = Type;
    Sym.n_sect = Sect;
    Sym.n_desc = Desc;
    Sym.n_value = Value;
    SC.Symbols.push_back(Sym);
    return {SC, SC.Symbols.size() - 1};
  }

  /// Lay out the MachO and return the total size of the resulting file.
  ///
  /// This method will automatically insert some load commands (e.g.
  /// LC_SYMTAB) and fill in load command fields.
  /// @return Total size in bytes of the laid-out MachO file.
  size_t layout() {

    // Build symbol table and add LC_SYMTAB command.
    makeStringTable();
    MachOBuilderLoadCommand<MachOTraits::SymTabCmd> *SymTabLC = nullptr;
    if (!StrTab.empty())
      SymTabLC = &addLoadCommand<MachOTraits::SymTabCmd>();

    // Lay out header, segment load command, and other load commands.
    size_t Offset = sizeof(Header);
    for (auto &Seg : Segments) {
      Seg.cmdsize +=
          Seg.Sections.size() * sizeof(typename MachOTraits::Section);
      Seg.nsects = Seg.Sections.size();
      Offset += Seg.cmdsize;
    }
    for (auto &LC : LoadCommands)
      Offset += LC->size();

    Header.sizeofcmds = Offset - sizeof(Header);

    // Lay out content, set segment / section addrs and offsets.
    size_t SegVMAddr = 0;
    for (auto &Seg : Segments) {
      Seg.vmaddr = SegVMAddr;
      Seg.fileoff = Offset;
      for (auto &Sec : Seg.Sections) {
        Offset = alignTo(Offset, 1ULL << Sec->align);
        if (Sec->Content.Size)
          Sec->offset = Offset;
        Sec->size = Sec->Content.Size;
        Sec->addr = SegVMAddr + Sec->offset - Seg.fileoff;
        Offset += Sec->Content.Size;
      }
      size_t SegContentSize = Offset - Seg.fileoff;
      Seg.filesize = SegContentSize;
      Seg.vmsize = Header.filetype == MachO::MH_OBJECT
                       ? SegContentSize
                       : alignTo(SegContentSize, PageSize);
      SegVMAddr += Seg.vmsize;
    }

    // Set string table offsets for non-section symbols.
    for (auto &Sym : SC.Symbols)
      Sym.n_strx = StrTab[Sym.n_strx].Offset;

    // Number sections, set symbol section numbers and string table offsets,
    // count relocations.
    size_t NumSymbols = SC.Symbols.size();
    size_t SectionNumber = 0;
    for (auto &Seg : Segments) {
      for (auto &Sec : Seg.Sections) {
        ++SectionNumber;
        Sec->SectionNumber = SectionNumber;
        Sec->SC.SymbolIndexBase = NumSymbols;
        NumSymbols += Sec->SC.Symbols.size();
        for (auto &Sym : Sec->SC.Symbols) {
          Sym.n_sect = SectionNumber;
          Sym.n_strx = StrTab[Sym.n_strx].Offset;
          Sym.n_value += Sec->addr;
        }
      }
    }

    // Handle relocations
    bool OffsetAlignedForRelocs = false;
    for (auto &Seg : Segments) {
      for (auto &Sec : Seg.Sections) {
        if (!Sec->Relocs.empty()) {
          if (!OffsetAlignedForRelocs) {
            Offset = alignTo(Offset, sizeof(MachO::relocation_info));
            OffsetAlignedForRelocs = true;
          }
          Sec->reloff = Offset;
          Sec->nreloc = Sec->Relocs.size();
          Offset += Sec->Relocs.size() * sizeof(MachO::relocation_info);
          for (auto &R : Sec->Relocs)
            R.r_symbolnum = R.Target.isSymbol() ? R.Target.getSymbolNum()
                                                : R.Target.getSectionId();
        }
      }
    }

    // Calculate offset to start of nlist and update symtab command.
    if (NumSymbols > 0) {
      Offset = alignTo(Offset, sizeof(typename MachOTraits::NList));
      SymTabLC->symoff = Offset;
      SymTabLC->nsyms = NumSymbols;

      // Calculate string table bounds and update symtab command.
      if (!StrTab.empty()) {
        Offset += NumSymbols * sizeof(typename MachOTraits::NList);
        size_t StringTableSize =
            StrTab.back().Offset + StrTab.back().S.size() + 1;

        SymTabLC->stroff = Offset;
        SymTabLC->strsize = StringTableSize;
        Offset += StringTableSize;
      }
    }

    return Offset;
  }

  /// Write the fully laid-out MachO object into \p Buffer.
  /// @param Buffer Destination buffer sized to at least the value from layout.
  void write(MutableArrayRef<char> Buffer) {
    size_t Offset = 0;
    Offset = writeHeader(Buffer, Offset);
    Offset = writeSegments(Buffer, Offset);
    Offset = writeLoadCommands(Buffer, Offset);
    Offset = writeSectionContent(Buffer, Offset);
    Offset = writeRelocations(Buffer, Offset);
    Offset = writeSymbols(Buffer, Offset);
    Offset = writeStrings(Buffer, Offset);
  }

  /// MachO object header for the object being built.
  typename MachOTraits::Header Header;

private:
  void makeStringTable() {
    if (Strings.empty())
      return;

    StrTab.resize(Strings.size());
    for (auto &[Str, Idx] : Strings)
      StrTab[Idx] = {Str, 0};
    size_t Offset = 0;
    for (auto &Elem : StrTab) {
      Elem.Offset = Offset;
      Offset += Elem.S.size() + 1;
    }
  }

  size_t writeHeader(MutableArrayRef<char> Buf, size_t Offset) {
    Header.ncmds = Segments.size() + LoadCommands.size();
    return writeMachOStruct(Buf, Offset, Header, swapStruct());
  }

  size_t writeSegments(MutableArrayRef<char> Buf, size_t Offset) {
    for (auto &Seg : Segments)
      Offset = Seg.write(Buf, Offset, swapStruct());
    return Offset;
  }

  size_t writeLoadCommands(MutableArrayRef<char> Buf, size_t Offset) {
    for (auto &LC : LoadCommands)
      Offset = LC->write(Buf, Offset, swapStruct());
    return Offset;
  }

  size_t writeSectionContent(MutableArrayRef<char> Buf, size_t Offset) {
    for (auto &Seg : Segments) {
      for (auto &Sec : Seg.Sections) {
        if (!Sec->Content.Data) {
          assert(Sec->Relocs.empty() &&
                 "Cant' have relocs for zero-fill segment");
          continue;
        }
        while (Offset != Sec->offset)
          Buf[Offset++] = '\0';

        assert(Offset + Sec->Content.Size <= Buf.size() && "Buffer overflow");
        memcpy(&Buf[Offset], Sec->Content.Data, Sec->Content.Size);
        Offset += Sec->Content.Size;
      }
    }
    return Offset;
  }

  size_t writeRelocations(MutableArrayRef<char> Buf, size_t Offset) {
    for (auto &Seg : Segments) {
      for (auto &Sec : Seg.Sections) {
        if (!Sec->Relocs.empty()) {
          while (Offset % sizeof(MachO::relocation_info))
            Buf[Offset++] = '\0';
        }
        for (auto &R : Sec->Relocs) {
          assert(Offset + sizeof(MachO::relocation_info) <= Buf.size() &&
                 "Buffer overflow");
          memcpy(&Buf[Offset], reinterpret_cast<const char *>(&R.rawStruct()),
                 sizeof(MachO::relocation_info));
          Offset += sizeof(MachO::relocation_info);
        }
      }
    }
    return Offset;
  }

  size_t writeSymbols(MutableArrayRef<char> Buf, size_t Offset) {

    // Count symbols.
    size_t NumSymbols = SC.Symbols.size();
    for (auto &Seg : Segments)
      for (auto &Sec : Seg.Sections)
        NumSymbols += Sec->SC.Symbols.size();

    // If none then return.
    if (NumSymbols == 0)
      return Offset;

    // Align to nlist entry size.
    while (Offset % sizeof(typename MachOTraits::NList))
      Buf[Offset++] = '\0';

    // Write non-section symbols.
    for (auto &Sym : SC.Symbols)
      Offset = writeMachOStruct(Buf, Offset, Sym, swapStruct());

    // Write section symbols.
    for (auto &Seg : Segments) {
      for (auto &Sec : Seg.Sections) {
        for (auto &Sym : Sec->SC.Symbols) {
          Offset = writeMachOStruct(Buf, Offset, Sym, swapStruct());
        }
      }
    }
    return Offset;
  }

  size_t writeStrings(MutableArrayRef<char> Buf, size_t Offset) {
    for (auto &Elem : StrTab) {
      assert(Offset + Elem.S.size() + 1 <= Buf.size() && "Buffer overflow");
      memcpy(&Buf[Offset], Elem.S.data(), Elem.S.size());
      Offset += Elem.S.size();
      Buf[Offset++] = '\0';
    }
    return Offset;
  }

  size_t PageSize;
  std::list<Segment> Segments;
  std::vector<std::unique_ptr<MachOBuilderLoadCommandBase>> LoadCommands;
  SymbolContainer SC;

  // Maps strings to their "id" (addition order).
  std::map<StringRef, size_t> Strings;
  StringTable StrTab;
};

/// MachO traits for little-endian 64-bit MachO objects.
struct MachO64LE {
  /// Pointer-sized integer type for this MachO variant.
  using UIntPtr = uint64_t;
  /// MachO header type for this variant.
  using Header = MachO::mach_header_64;
  /// MachO section header type for this variant.
  using Section = MachO::section_64;
  /// MachO nlist entry type for this variant.
  using NList = MachO::nlist_64;
  /// MachO relocation entry type for this variant.
  using Relocation = MachO::relocation_info;

  /// Byte order of this MachO variant.
  static constexpr llvm::endianness Endianness = llvm::endianness::little;
  /// MachO magic value for this variant.
  static constexpr uint32_t Magic = MachO::MH_MAGIC_64;
  /// Load command type used for segments in this variant.
  static constexpr MachO::LoadCommandType SegmentCmd = MachO::LC_SEGMENT_64;
  /// Load command type used for the symbol table in this variant.
  static constexpr MachO::LoadCommandType SymTabCmd = MachO::LC_SYMTAB;
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_MACHOBUILDER_H
