//===-- LVBinaryReader.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVBinaryReader class, which is used to describe a
// binary reader.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVBINARYREADER_H
#define LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVBINARYREADER_H

#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Object/ObjectFile.h"

namespace llvm {
namespace logicalview {

/// Whether high addresses should be updated while merging ranges.
constexpr bool UpdateHighAddress = false;

/// Logical scope, section address, section index, and COMDAT flag for a symbol.
struct LVSymbolTableEntry final {
  /// Logical scope associated with this symbol table entry.
  LVScope *Scope = nullptr;
  /// Absolute or section-relative address for the symbol.
  LVAddress Address = 0;
  /// Object-file section index that contains the symbol.
  LVSectionIndex SectionIndex = 0;
  /// True when the symbol belongs to a COMDAT section.
  bool IsComdat = false;
  /// Construct an empty symbol table entry with default field values.
  LVSymbolTableEntry() = default;
  /// Construct a symbol table entry from scope, address, section, and COMDAT.
  ///
  /// \param Scope Logical scope associated with the symbol.
  /// \param Address Absolute or section-relative address for the symbol.
  /// \param SectionIndex Object-file section index that contains the symbol.
  /// \param IsComdat True when the symbol belongs to a COMDAT section.
  LVSymbolTableEntry(LVScope *Scope, LVAddress Address,
                     LVSectionIndex SectionIndex, bool IsComdat)
      : Scope(Scope), Address(Address), SectionIndex(SectionIndex),
        IsComdat(IsComdat) {}
};

/// Function names extracted from the object symbol table.
class LVSymbolTable final {
  using LVSymbolNames = std::map<std::string, LVSymbolTableEntry, std::less<>>;
  LVSymbolNames SymbolNames;

public:
  /// Construct an empty symbol table.
  LVSymbolTable() = default;

  /// Record or update a function scope under \p Name in the symbol table.
  ///
  /// \param Name Linkage or symbol name used as the table key.
  /// \param Function Logical function scope associated with the name.
  /// \param SectionIndex Object-file section index for the function.
  LLVM_ABI void add(StringRef Name, LVScope *Function,
                    LVSectionIndex SectionIndex = 0);
  /// Record a symbol address, section index, and COMDAT flag under \p Name.
  ///
  /// \param Name Linkage or symbol name used as the table key.
  /// \param Address Absolute or section-relative address for the symbol.
  /// \param SectionIndex Object-file section index that contains the symbol.
  /// \param IsComdat True when the symbol belongs to a COMDAT section.
  LLVM_ABI void add(StringRef Name, LVAddress Address,
                    LVSectionIndex SectionIndex, bool IsComdat);
  /// Update the table entry for \p Function and return its section index.
  ///
  /// \param Function Logical function scope whose table entry is updated.
  /// \returns Section index associated with \p Function after the update.
  LLVM_ABI LVSectionIndex update(LVScope *Function);

  /// Return the symbol table entry for \p Name.
  /// \param Name Linkage or symbol name used as the table key.
  /// \returns Symbol table entry for \p Name.
  LLVM_ABI const LVSymbolTableEntry &getEntry(StringRef Name);
  /// Return the recorded address for \p Name.
  /// \param Name Linkage or symbol name used as the table key.
  /// \returns Recorded address for \p Name.
  LLVM_ABI LVAddress getAddress(StringRef Name);
  /// Return the recorded section index for \p Name.
  /// \param Name Linkage or symbol name used as the table key.
  /// \returns Recorded section index for \p Name.
  LLVM_ABI LVSectionIndex getIndex(StringRef Name);
  /// Return whether the symbol named \p Name is marked as COMDAT.
  /// \param Name Linkage or symbol name used as the table key.
  /// \returns True when the symbol named \p Name is marked as COMDAT.
  LLVM_ABI bool getIsComdat(StringRef Name);

  /// Print the symbol table contents to \p OS.
  /// \param OS Output stream to write to.
  LLVM_ABI void print(raw_ostream &OS);
};

/// Reader that builds a logical view from a binary object file.
class LVBinaryReader : public LVReader {
  // Function names extracted from the object symbol table.
  LVSymbolTable SymbolTable;

  // It contains the LVLineDebug elements representing the inlined logical
  // lines for the current compile unit, created by parsing the CodeView
  // S_INLINESITE symbol annotation data.
  using LVInlineeLine = std::map<LVScope *, std::unique_ptr<LVLines>>;
  LVInlineeLine CUInlineeLines;

  // Instruction lines for a logical scope. These instructions are fetched
  // during its merge with the debug lines.
  LVDoubleMap<LVSectionIndex, LVScope *, LVLines *> ScopeInstructions;

  // Links the scope with its first assembler address line.
  LVDoubleMap<LVSectionIndex, LVAddress, LVScope *> AssemblerMappings;

  // Mapping from virtual address to section.
  // The virtual address refers to the address where the section is loaded.
  using LVSectionAddresses = std::map<LVSectionIndex, object::SectionRef>;
  LVSectionAddresses SectionAddresses;

  void addSectionAddress(const object::SectionRef &Section) {
    if (SectionAddresses.find(Section.getAddress()) == SectionAddresses.end())
      SectionAddresses.emplace(Section.getAddress(), Section);
  }

  // Image base and virtual address for Executable file.
  uint64_t ImageBaseAddress = 0;
  uint64_t VirtualAddress = 0;

  // Object sections with machine code.
  using LVSections = std::map<LVSectionIndex, object::SectionRef>;
  LVSections Sections;

  std::vector<std::unique_ptr<LVLines>> DiscoveredLines;

protected:
  /// Logical debug lines for the current compile unit from the debug line
  /// section.
  LVLines CULines;

  /// Target register information used when disassembling instructions.
  std::unique_ptr<const MCRegisterInfo> MRI;
  /// Target options used to configure the MC layer.
  MCTargetOptions MCOptions;
  /// Target assembly information used when printing instructions.
  std::unique_ptr<const MCAsmInfo> MAI;
  /// Subtarget information describing the object file architecture.
  std::unique_ptr<const MCSubtargetInfo> STI;
  /// Instruction information used by the disassembler and printer.
  std::unique_ptr<const MCInstrInfo> MII;
  /// Disassembler used to decode machine instructions from sections.
  std::unique_ptr<const MCDisassembler> MD;
  /// MC context shared by the disassembler and instruction printer.
  std::unique_ptr<MCContext> MC;
  /// Instruction printer used to format disassembled opcodes.
  std::unique_ptr<MCInstPrinter> MIP;

  /// Offset of the WebAssembly Code section used as the DWARF code base.
  ///
  /// https://yurydelendik.github.io/webassembly-dwarf/
  /// 2. Consuming and Generating DWARF for WebAssembly Code
  /// Note: Some DWARF constructs don't map one-to-one onto WebAssembly
  /// constructs. We strive to enumerate and resolve any ambiguities here.
  ///
  /// 2.1. Code Addresses
  /// Note: DWARF associates various bits of debug info
  /// with particular locations in the program via its code address (instruction
  /// pointer or PC). However, WebAssembly's linear memory address space does not
  /// contain WebAssembly instructions.
  ///
  /// Wherever a code address (see 2.17 of [DWARF]) is used in DWARF for
  /// WebAssembly, it must be the offset of an instruction relative within the
  /// Code section of the WebAssembly file. The DWARF is considered malformed if
  /// a PC offset is between instruction boundaries within the Code section.
  ///
  /// Note: It is expected that a DWARF consumer does not know how to decode
  /// WebAssembly instructions. The instruction pointer is selected as the offset
  /// in the binary file of the first byte of the instruction, and it is
  /// consistent with the WebAssembly Web API conventions definition of the code
  /// location.
  ///
  /// EXAMPLE: .DEBUG_LINE INSTRUCTION POINTERS
  /// The .debug_line DWARF section maps instruction pointers to source
  /// locations. With WebAssembly, the .debug_line section maps Code
  /// section-relative instruction offsets to source locations.
  ///
  /// EXAMPLE: DW_AT_* ATTRIBUTES
  /// For entities with a single associated code address, DWARF uses
  /// the DW_AT_low_pc attribute to specify the associated code address value.
  /// For WebAssembly, the DW_AT_low_pc's value is a Code section-relative
  /// instruction offset.
  ///
  /// For entities with a single contiguous range of code, DWARF uses a
  /// pair of DW_AT_low_pc and DW_AT_high_pc attributes to specify the associated
  /// contiguous range of code address values. For WebAssembly, these attributes
  /// are Code section-relative instruction offsets.
  ///
  /// For entities with multiple ranges of code, DWARF uses the DW_AT_ranges
  /// attribute, which refers to the array located at the .debug_ranges section.
  LVAddress WasmCodeSectionOffset = 0;

  /// Load MC target components for the given triple, features, and CPU.
  ///
  /// \param TheTriple Target triple describing the object architecture.
  /// \param TheFeatures Feature string passed to the subtarget.
  /// \param TheCPU CPU name passed to the subtarget.
  /// \returns Success or an error if target components could not be loaded.
  LLVM_ABI Error loadGenericTargetInfo(StringRef TheTriple,
                                       StringRef TheFeatures, StringRef TheCPU);

  /// Map address ranges for all sections in \p Obj (default no-op).
  /// \param Obj Object file whose address ranges are mapped.
  virtual void mapRangeAddress(const object::ObjectFile &Obj) {}
  /// Map address ranges for a single section, noting COMDAT membership.
  ///
  /// \param Obj Object file that owns \p Section.
  /// \param Section Section whose address range is mapped.
  /// \param IsComdat True when \p Section is a COMDAT section.
  virtual void mapRangeAddress(const object::ObjectFile &Obj,
                               const object::SectionRef &Section,
                               bool IsComdat) {}

  /// Create a mapping from virtual address to section for \p Obj.
  /// \param Obj Object file whose section virtual addresses are mapped.
  LLVM_ABI void mapVirtualAddress(const object::ObjectFile &Obj);
  /// Create a mapping from virtual address to section for a COFF object.
  /// \param COFFObj COFF object whose section virtual addresses are mapped.
  LLVM_ABI void mapVirtualAddress(const object::COFFObjectFile &COFFObj);

  /// Resolve the section that contains \p Address for \p Scope.
  ///
  /// \param Scope Logical scope used when selecting a section.
  /// \param Address Address to locate within the object sections.
  /// \param SectionIndex Preferred section index, when known.
  /// \returns Section index and section reference, or an error on failure.
  LLVM_ABI Expected<std::pair<LVSectionIndex, object::SectionRef>>
  getSection(LVScope *Scope, LVAddress Address, LVSectionIndex SectionIndex);

  /// Merge inlinee lines for \p Function into the lines for \p SectionIndex.
  ///
  /// \param SectionIndex Section whose lines receive the inlinee entries.
  /// \param Function Function scope whose inlinee lines are included.
  LLVM_ABI void includeInlineeLines(LVSectionIndex SectionIndex,
                                    LVScope *Function);

  /// Disassemble instructions for all recorded function scopes.
  /// \returns Success or an error if instructions could not be created.
  LLVM_ABI Error createInstructions();
  /// Disassemble instructions for \p Function in \p SectionIndex.
  ///
  /// \param Function Function scope whose instructions are created.
  /// \param SectionIndex Section that contains the function body.
  /// \returns Success or an error if instructions could not be created.
  LLVM_ABI Error createInstructions(LVScope *Function,
                                    LVSectionIndex SectionIndex);
  /// Disassemble instructions for \p Function using \p NameInfo bounds.
  ///
  /// \param Function Function scope whose instructions are created.
  /// \param SectionIndex Section that contains the function body.
  /// \param NameInfo Name and size information describing the function range.
  /// \returns Success or an error if instructions could not be created.
  LLVM_ABI Error createInstructions(LVScope *Function,
                                    LVSectionIndex SectionIndex,
                                    const LVNameInfo &NameInfo);

  /// Merge \p DebugLines with disassembled instructions for \p SectionIndex.
  ///
  /// \param DebugLines Debug line records to merge with instructions.
  /// \param SectionIndex Section whose lines and instructions are processed.
  LLVM_ABI void processLines(LVLines *DebugLines, LVSectionIndex SectionIndex);
  /// Merge \p DebugLines with instructions for \p Function in \p SectionIndex.
  ///
  /// \param DebugLines Debug line records to merge with instructions.
  /// \param SectionIndex Section whose lines and instructions are processed.
  /// \param Function Function scope that owns the lines being processed.
  LLVM_ABI void processLines(LVLines *DebugLines, LVSectionIndex SectionIndex,
                             LVScope *Function);

public:
  /// Deleted; a binary reader requires an input file and printer.
  LVBinaryReader() = delete;
  /// Construct a binary reader for \p Filename with the given format and type.
  ///
  /// \param Filename Path of the input file being read.
  /// \param FileFormatName Human-readable object format name.
  /// \param W Printer used for diagnostic and dump output.
  /// \param BinaryType Binary format classification for the input file.
  LVBinaryReader(StringRef Filename, StringRef FileFormatName, ScopedPrinter &W,
                 LVBinaryType BinaryType)
      : LVReader(Filename, FileFormatName, W, BinaryType) {}
  /// Deleted; binary readers are not copyable.
  /// \param Other Unused source reader.
  LVBinaryReader(const LVBinaryReader &Other) = delete;
  /// Deleted; binary readers are not copy-assignable.
  /// \param Other Unused source reader.
  LVBinaryReader &operator=(const LVBinaryReader &Other) = delete;
  /// Destroy the binary reader.
  ~LVBinaryReader() override = default;

  /// Store inlinee lines for \p Scope in the current compile unit.
  ///
  /// \param Scope Scope that owns the inlinee line records.
  /// \param Lines Inlinee logical lines to associate with \p Scope.
  void addInlineeLines(LVScope *Scope, LVLines &Lines) {
    CUInlineeLines.emplace(Scope, std::make_unique<LVLines>(std::move(Lines)));
  }

  /// Convert a Segment::Offset pair to an absolute address.
  ///
  /// \param Segment Segment selector used when forming the linear address.
  /// \param Offset Offset within the segment.
  /// \param Addendum Optional value added to the computed address.
  /// \returns Absolute linear address for the segment, offset, and addendum.
  LVAddress linearAddress(uint16_t Segment, uint32_t Offset,
                          LVAddress Addendum = 0) {
    return ImageBaseAddress + (Segment * VirtualAddress) + Offset + Addendum;
  }

  /// Record or update a function scope under \p Name in the symbol table.
  ///
  /// \param Name Linkage or symbol name used as the table key.
  /// \param Function Logical function scope associated with the name.
  /// \param SectionIndex Object-file section index for the function.
  LLVM_ABI void addToSymbolTable(StringRef Name, LVScope *Function,
                                 LVSectionIndex SectionIndex = 0);
  /// Record a symbol address, section index, and COMDAT flag under \p Name.
  ///
  /// \param Name Linkage or symbol name used as the table key.
  /// \param Address Absolute or section-relative address for the symbol.
  /// \param SectionIndex Object-file section index that contains the symbol.
  /// \param IsComdat True when the symbol belongs to a COMDAT section.
  LLVM_ABI void addToSymbolTable(StringRef Name, LVAddress Address,
                                 LVSectionIndex SectionIndex, bool IsComdat);
  /// Update the symbol table entry for \p Function and return its section
  /// index.
  ///
  /// \param Function Logical function scope whose table entry is updated.
  /// \returns Section index associated with \p Function after the update.
  LLVM_ABI LVSectionIndex updateSymbolTable(LVScope *Function);

  /// Return the symbol table entry for \p Name.
  /// \param Name Linkage or symbol name used as the table key.
  /// \returns Symbol table entry for \p Name.
  LLVM_ABI const LVSymbolTableEntry &getSymbolTableEntry(StringRef Name);
  /// Return the recorded address for \p Name from the symbol table.
  /// \param Name Linkage or symbol name used as the table key.
  /// \returns Recorded address for \p Name from the symbol table.
  LLVM_ABI LVAddress getSymbolTableAddress(StringRef Name);
  /// Return the recorded section index for \p Name from the symbol table.
  /// \param Name Linkage or symbol name used as the table key.
  /// \returns Recorded section index for \p Name from the symbol table.
  LLVM_ABI LVSectionIndex getSymbolTableIndex(StringRef Name);
  /// Return whether the symbol named \p Name is marked as COMDAT.
  /// \param Name Linkage or symbol name used as the table key.
  /// \returns True when the symbol named \p Name is marked as COMDAT.
  LLVM_ABI bool getSymbolTableIsComdat(StringRef Name);

  /// Return the section index for \p Scope, or the `.text` index if null.
  /// \param Scope Scope whose linkage name selects a symbol table entry.
  /// \returns Section index for \p Scope, or the `.text` index when null.
  LVSectionIndex getSectionIndex(LVScope *Scope) override {
    return Scope ? getSymbolTableIndex(Scope->getLinkageName())
                 : DotTextSectionIndex;
  }

  /// Print a debug summary of this reader to \p OS.
  /// \param OS Output stream to write to.
  LLVM_ABI void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this reader to the debug stream.
  void dump() const { print(dbgs()); }
#endif
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVBINARYREADER_H
