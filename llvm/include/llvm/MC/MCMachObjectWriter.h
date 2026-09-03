//===- llvm/MC/MCMachObjectWriter.h - Mach Object Writer --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCMACHOBJECTWRITER_H
#define LLVM_MC_MCMACHOBJECTWRITER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCLinkerOptimizationHint.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSymbolMachO.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/VersionTuple.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {

class MachObjectWriter;

/// Target-specific Mach-O object-file writer hooks.
class LLVM_ABI MCMachObjectTargetWriter : public MCObjectTargetWriter {
  const unsigned Is64Bit : 1;
  const uint32_t CPUType;
protected:
  /// Mach-O CPU subtype for the target architecture.
  uint32_t CPUSubtype;
public:
  /// Relocation type used for local difference expressions, or 0 if unused.
  unsigned LocalDifference_RIT = 0;

protected:
  /// Construct a Mach-O target object writer.
  ///
  /// \param Is64Bit_ - True if writing a 64-bit Mach-O object.
  /// \param CPUType_ - Mach-O CPU type for the header.
  /// \param CPUSubtype_ - Mach-O CPU subtype for the header.
  MCMachObjectTargetWriter(bool Is64Bit_, uint32_t CPUType_,
                           uint32_t CPUSubtype_);

  /// Set the relocation type used for local difference expressions.
  ///
  /// \param Type - Mach-O relocation type for local differences.
  void setLocalDifferenceRelocationType(unsigned Type) {
    LocalDifference_RIT = Type;
  }

public:
  /// Destroy the Mach-O target object writer.
  ~MCMachObjectTargetWriter() override;

  /// Return the object file format handled by this writer.
  ///
  /// \return The object file format, always \c Triple::MachO.
  Triple::ObjectFormatType getFormat() const override { return Triple::MachO; }
  /// Return true if \p W is a Mach-O target object writer.
  ///
  /// \param W - Object target writer to test.
  /// \return True if \p W is a Mach-O target object writer.
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::MachO;
  }

  /// \name Lifetime Management
  /// @{

  /// Reset target writer state for reuse.
  virtual void reset() {}

  /// @}

  /// \name Accessors
  /// @{

  /// Return true if this writer targets 64-bit Mach-O.
  ///
  /// \return True if this writer targets 64-bit Mach-O.
  bool is64Bit() const { return Is64Bit; }
  /// Return the Mach-O CPU type for this writer.
  ///
  /// \return The Mach-O CPU type for this writer.
  uint32_t getCPUType() const { return CPUType; }
  /// Return the Mach-O CPU subtype for this writer.
  ///
  /// \return The Mach-O CPU subtype for this writer.
  uint32_t getCPUSubtype() const { return CPUSubtype; }
  /// Return the relocation type used for local difference expressions.
  ///
  /// \return The relocation type used for local difference expressions.
  unsigned getLocalDifferenceRelocationType() const {
    return LocalDifference_RIT;
  }

  /// @}

  /// \name API
  /// @{

  /// Record a Mach-O relocation for \p Fixup in \p Fragment.
  ///
  /// \param Writer - Mach-O object writer requesting the relocation.
  /// \param Asm - Assembler providing layout and symbol information.
  /// \param Fragment - Fragment containing the fixup.
  /// \param Fixup - Fixup to record as a relocation.
  /// \param Target - Relocatable expression for the fixup.
  /// \param FixedValue - [out] Value to encode into the fragment.
  virtual void recordRelocation(MachObjectWriter *Writer, MCAssembler &Asm,
                                const MCFragment *Fragment,
                                const MCFixup &Fixup, MCValue Target,
                                uint64_t &FixedValue) = 0;

  /// @}
};

/// Mach-O object writer that emits Mach-O object files from an MCAssembler.
class LLVM_ABI MachObjectWriter final : public MCObjectWriter {
public:
  /// A data-in-code or similar region delimited by start and end symbols.
  struct DataRegionData {
    /// Kind of data region to emit.
    MachO::DataRegionType Kind;
    /// Symbol marking the start of the region.
    MCSymbol *Start;
    /// Symbol marking the end of the region.
    MCSymbol *End;
  };

  /// Deployment-target or build-version information for a Mach-O load command.
  ///
  /// A Major version of 0 indicates that no version information was supplied
  /// and so the corresponding load command should not be emitted.
  using VersionInfoType = struct {
    /// True to emit LC_BUILD_VERSION instead of a version-min load command.
    bool EmitBuildVersion;
    /// Holds either a version-min type or a build-version platform.
    union {
      MCVersionMinType Type;        ///< Used when EmitBuildVersion==false.
      MachO::PlatformType Platform; ///< Used when EmitBuildVersion==true.
    } TypeOrPlatform; ///< Version-min type or platform selected by EmitBuildVersion.
    /// Major component of the deployment or build version.
    unsigned Major;
    /// Minor component of the deployment or build version.
    unsigned Minor;
    /// Update component of the deployment or build version.
    unsigned Update;
    /// An optional version of the SDK that was used to build the source.
    VersionTuple SDKVersion;
  };

private:
  /// Helper struct for containing some precomputed information on symbols.
  struct MachSymbolData {
    const MCSymbolMachO *Symbol;
    uint64_t StringIndex;
    uint8_t SectionIndex;

    // Support lexicographic sorting.
    LLVM_ABI bool operator<(const MachSymbolData &RHS) const;
  };

  struct IndirectSymbolData {
    MCSymbolMachO *Symbol;
    MCSection *Section;
  };

  /// The target specific Mach-O writer instance.
  std::unique_ptr<MCMachObjectTargetWriter> TargetObjectWriter;

  /// \name Relocation Data
  /// @{

  struct RelAndSymbol {
    const MCSymbol *Sym;
    MachO::any_relocation_info MRE;
    RelAndSymbol(const MCSymbol *Sym, const MachO::any_relocation_info &MRE)
        : Sym(Sym), MRE(MRE) {}
  };

  DenseMap<const MCSection *, std::vector<RelAndSymbol>> Relocations;
  std::vector<IndirectSymbolData> IndirectSymbols;
  DenseMap<const MCSection *, unsigned> IndirectSymBase;

  std::vector<DataRegionData> DataRegions;

  DenseMap<const MCSection *, uint64_t> SectionAddress;

  // List of sections in layout order. Virtual sections are after non-virtual
  // sections.
  SmallVector<MCSection *, 0> SectionOrder;

  /// @}
  /// \name Symbol Table Data
  /// @{

  StringTableBuilder StringTable;
  std::vector<MachSymbolData> LocalSymbolData;
  std::vector<MachSymbolData> ExternalSymbolData;
  std::vector<MachSymbolData> UndefinedSymbolData;

  /// @}

  // Used to communicate Linker Optimization Hint information.
  MCLOHContainer LOHContainer;

  VersionInfoType VersionInfo{};
  VersionInfoType TargetVariantVersionInfo{};

  std::optional<std::string> TargetTriple;

  // The list of linker options for LC_LINKER_OPTION.
  std::vector<std::vector<std::string>> LinkerOptions;

  MachSymbolData *findSymbolData(const MCSymbol &Sym);

  void writeWithPadding(StringRef Str, uint64_t Size);

public:
  /// Construct a Mach-O object writer for the given target and stream.
  ///
  /// \param MOTW - Target-specific Mach-O writer.
  /// \param OS - Stream to write the object to.
  /// \param IsLittleEndian - True to emit little-endian Mach-O.
  MachObjectWriter(std::unique_ptr<MCMachObjectTargetWriter> MOTW,
                   raw_pwrite_stream &OS, bool IsLittleEndian)
      : TargetObjectWriter(std::move(MOTW)),
        StringTable(TargetObjectWriter->is64Bit() ? StringTableBuilder::MachO64
                                                  : StringTableBuilder::MachO),
        W(OS,
          IsLittleEndian ? llvm::endianness::little : llvm::endianness::big) {}

  /// Endian-aware writer used for Mach-O payload emission.
  support::endian::Writer W;

  /// Return the non-aliased symbol that \p Sym ultimately refers to.
  ///
  /// \param Sym - Symbol that may be an alias.
  /// \return The non-aliased symbol that \p Sym ultimately refers to.
  const MCSymbol &findAliasedSymbol(const MCSymbol &Sym) const;

  /// Reset writer state for reuse.
  void reset() override;
  /// Set the assembler used by this writer.
  ///
  /// \param Asm - Assembler instance to associate.
  void setAssembler(MCAssembler *Asm) override;

  /// \name Utility Methods
  /// @{

  /// Return the list of indirect symbols to emit.
  ///
  /// \return The list of indirect symbols to emit.
  std::vector<IndirectSymbolData> &getIndirectSymbols() {
    return IndirectSymbols;
  }
  /// Return the list of data regions to emit.
  ///
  /// \return The list of data regions to emit.
  std::vector<DataRegionData> &getDataRegions() { return DataRegions; }
  /// Return sections in layout order.
  ///
  /// \return Sections in layout order.
  const llvm::SmallVectorImpl<MCSection *> &getSectionOrder() const {
    return SectionOrder;
  }
  /// Return the linker optimization hint container.
  ///
  /// \return The linker optimization hint container.
  MCLOHContainer &getLOHContainer() { return LOHContainer; }

  /// Return the address assigned to section \p Sec.
  ///
  /// \param Sec - Section whose address is queried.
  /// \return The address assigned to \p Sec.
  uint64_t getSectionAddress(const MCSection *Sec) const {
    return SectionAddress.lookup(Sec);
  }
  /// Return the address of symbol \p S in the object layout.
  ///
  /// \param S - Symbol whose address is queried.
  /// \return The address of \p S in the object layout.
  uint64_t getSymbolAddress(const MCSymbol &S) const;

  /// Return the address of \p Fragment within the object layout.
  ///
  /// \param Asm - Assembler providing layout information.
  /// \param Fragment - Fragment whose address is queried.
  /// \return The address of \p Fragment within the object layout.
  uint64_t getFragmentAddress(const MCAssembler &Asm,
                              const MCFragment *Fragment) const;

  /// Return the padding inserted after section \p SD.
  ///
  /// \param Asm - Assembler providing layout information.
  /// \param SD - Section whose trailing padding is queried.
  /// \return The padding size in bytes inserted after \p SD.
  uint64_t getPaddingSize(const MCAssembler &Asm, const MCSection *SD) const;

  /// Return the atom containing symbol \p S, or null if none.
  ///
  /// \param S - Symbol whose atom is queried.
  /// \return The atom containing \p S, or null if none.
  const MCSymbol *getAtom(const MCSymbol &S) const;

  /// Return true if symbol \p S requires an external relocation.
  ///
  /// \param S - Symbol to test.
  /// \return True if \p S requires an external relocation.
  bool doesSymbolRequireExternRelocation(const MCSymbol &S);

  /// Mach-O deployment target version information.
  ///
  /// \param Type - Version-min load command kind to emit.
  /// \param Major - Major deployment version component.
  /// \param Minor - Minor deployment version component.
  /// \param Update - Update deployment version component.
  /// \param SDKVersion - Optional SDK version used to build the source.
  void setVersionMin(MCVersionMinType Type, unsigned Major, unsigned Minor,
                     unsigned Update,
                     VersionTuple SDKVersion = VersionTuple()) {
    VersionInfo.EmitBuildVersion = false;
    VersionInfo.TypeOrPlatform.Type = Type;
    VersionInfo.Major = Major;
    VersionInfo.Minor = Minor;
    VersionInfo.Update = Update;
    VersionInfo.SDKVersion = SDKVersion;
  }
  /// Set the LC_BUILD_VERSION load command contents.
  ///
  /// \param Platform - Mach-O platform for the build version.
  /// \param Major - Major build version component.
  /// \param Minor - Minor build version component.
  /// \param Update - Update build version component.
  /// \param SDKVersion - Optional SDK version used to build the source.
  void setBuildVersion(MachO::PlatformType Platform, unsigned Major,
                       unsigned Minor, unsigned Update,
                       VersionTuple SDKVersion = VersionTuple()) {
    VersionInfo.EmitBuildVersion = true;
    VersionInfo.TypeOrPlatform.Platform = Platform;
    VersionInfo.Major = Major;
    VersionInfo.Minor = Minor;
    VersionInfo.Update = Update;
    VersionInfo.SDKVersion = SDKVersion;
  }
  /// Set the target-variant LC_BUILD_VERSION load command contents.
  ///
  /// \param Platform - Mach-O platform for the target-variant build version.
  /// \param Major - Major build version component.
  /// \param Minor - Minor build version component.
  /// \param Update - Update build version component.
  /// \param SDKVersion - SDK version used to build the source.
  void setTargetVariantBuildVersion(MachO::PlatformType Platform,
                                    unsigned Major, unsigned Minor,
                                    unsigned Update, VersionTuple SDKVersion) {
    TargetVariantVersionInfo.EmitBuildVersion = true;
    TargetVariantVersionInfo.TypeOrPlatform.Platform = Platform;
    TargetVariantVersionInfo.Major = Major;
    TargetVariantVersionInfo.Minor = Minor;
    TargetVariantVersionInfo.Update = Update;
    TargetVariantVersionInfo.SDKVersion = SDKVersion;
  }

  /// Record the target triple string for emission.
  ///
  /// \param Triple - Target triple to store.
  void setTargetTriple(StringRef Triple) { TargetTriple = Triple; }

  /// Return the linker options collected for LC_LINKER_OPTION.
  ///
  /// \return The linker options collected for LC_LINKER_OPTION.
  std::vector<std::vector<std::string>> &getLinkerOptions() {
    return LinkerOptions;
  }

  /// @}

  /// \name Target Writer Proxy Accessors
  /// @{

  /// Return true if the target writer is for 64-bit Mach-O.
  ///
  /// \return True if the target writer is for 64-bit Mach-O.
  bool is64Bit() const { return TargetObjectWriter->is64Bit(); }
  /// Return true if the target writer is for x86_64.
  ///
  /// \return True if the target writer is for x86_64.
  bool isX86_64() const {
    uint32_t CPUType = TargetObjectWriter->getCPUType();
    return CPUType == MachO::CPU_TYPE_X86_64;
  }

  /// @}

  /// Write the Mach-O object file header.
  ///
  /// \param Type - Mach-O header file type.
  /// \param NumLoadCommands - Number of load commands that follow.
  /// \param LoadCommandsSize - Total size in bytes of all load commands.
  /// \param SubsectionsViaSymbols - True to set MH_SUBSECTIONS_VIA_SYMBOLS.
  void writeHeader(MachO::HeaderFileType Type, unsigned NumLoadCommands,
                   unsigned LoadCommandsSize, bool SubsectionsViaSymbols);

  /// Write a segment load command.
  ///
  /// \param Name - Segment name.
  /// \param NumSections - The number of sections in this segment.
  /// \param VMAddr - Virtual memory address of the segment.
  /// \param VMSize - Virtual memory size of the segment.
  /// \param SectionDataStartOffset - File offset where section data begins.
  /// \param SectionDataSize - The total size of the sections.
  /// \param MaxProt - Maximum virtual memory protections for the segment.
  /// \param InitProt - Initial virtual memory protections for the segment.
  void writeSegmentLoadCommand(StringRef Name, unsigned NumSections,
                               uint64_t VMAddr, uint64_t VMSize,
                               uint64_t SectionDataStartOffset,
                               uint64_t SectionDataSize, uint32_t MaxProt,
                               uint32_t InitProt);

  /// Write a section header for \p Sec.
  ///
  /// \param Asm - Assembler providing section contents and layout.
  /// \param Sec - Mach-O section to describe.
  /// \param VMAddr - Virtual address assigned to the section.
  /// \param FileOffset - File offset of the section data.
  /// \param Flags - Mach-O section flags to emit.
  /// \param RelocationsStart - File offset of this section's relocations.
  /// \param NumRelocations - Number of relocations for this section.
  void writeSection(const MCAssembler &Asm, const MCSectionMachO &Sec,
                    uint64_t VMAddr, uint64_t FileOffset, unsigned Flags,
                    uint64_t RelocationsStart, unsigned NumRelocations);

  /// Write an LC_SYMTAB load command.
  ///
  /// \param SymbolOffset - File offset of the symbol table.
  /// \param NumSymbols - Number of symbol table entries.
  /// \param StringTableOffset - File offset of the string table.
  /// \param StringTableSize - Size in bytes of the string table.
  void writeSymtabLoadCommand(uint32_t SymbolOffset, uint32_t NumSymbols,
                              uint32_t StringTableOffset,
                              uint32_t StringTableSize);

  /// Write an LC_DYSYMTAB load command.
  ///
  /// \param FirstLocalSymbol - Index of the first local symbol.
  /// \param NumLocalSymbols - Number of local symbols.
  /// \param FirstExternalSymbol - Index of the first external symbol.
  /// \param NumExternalSymbols - Number of external symbols.
  /// \param FirstUndefinedSymbol - Index of the first undefined symbol.
  /// \param NumUndefinedSymbols - Number of undefined symbols.
  /// \param IndirectSymbolOffset - File offset of the indirect symbol table.
  /// \param NumIndirectSymbols - Number of indirect symbol table entries.
  void writeDysymtabLoadCommand(
      uint32_t FirstLocalSymbol, uint32_t NumLocalSymbols,
      uint32_t FirstExternalSymbol, uint32_t NumExternalSymbols,
      uint32_t FirstUndefinedSymbol, uint32_t NumUndefinedSymbols,
      uint32_t IndirectSymbolOffset, uint32_t NumIndirectSymbols);

  /// Write one nlist entry for \p MSD.
  ///
  /// \param MSD - Precomputed symbol table data to emit.
  /// \param Asm - Assembler providing symbol attributes.
  void writeNlist(MachSymbolData &MSD, const MCAssembler &Asm);

  /// Write a linkedit-data style load command.
  ///
  /// \param Type - Mach-O load command type.
  /// \param DataOffset - File offset of the associated data.
  /// \param DataSize - Size in bytes of the associated data.
  void writeLinkeditLoadCommand(uint32_t Type, uint32_t DataOffset,
                                uint32_t DataSize);

  /// Write an LC_LINKER_OPTION load command for \p Options.
  ///
  /// \param Options - Linker option strings to emit.
  void writeLinkerOptionsLoadCommand(const std::vector<std::string> &Options);

  // FIXME: We really need to improve the relocation validation. Basically, we
  // want to implement a separate computation which evaluates the relocation
  // entry as the linker would, and verifies that the resultant fixup value is
  // exactly what the encoder wanted. This will catch several classes of
  // problems:
  //
  //  - Relocation entry bugs, the two algorithms are unlikely to have the same
  //    exact bug.
  //
  //  - Relaxation issues, where we forget to relax something.
  //
  //  - Input errors, where something cannot be correctly encoded. 'as' allows
  //    these through in many cases.

  /// Add a relocation to be output in the object file.
  ///
  /// At the time this is called, the symbol indexes are not known, so if the
  /// relocation refers to a symbol it should be passed as \p RelSymbol so that
  /// it can be updated afterwards. If the relocation doesn't refer to a
  /// symbol, nullptr should be used.
  ///
  /// \param RelSymbol - Symbol referred to by the relocation, or null.
  /// \param Sec - Section that owns the relocation.
  /// \param MRE - Mach-O relocation entry to store.
  void addRelocation(const MCSymbol *RelSymbol, const MCSection *Sec,
                     MachO::any_relocation_info &MRE) {
    RelAndSymbol P(RelSymbol, MRE);
    Relocations[Sec].push_back(P);
  }

  /// Record a Mach-O relocation for fixup \p Fixup in fragment \p F.
  ///
  /// \param F - Fragment containing the fixup.
  /// \param Fixup - Fixup to record as a relocation.
  /// \param Target - Relocatable expression for the fixup.
  /// \param FixedValue - [out] Value to encode into the fragment.
  void recordRelocation(const MCFragment &F, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override;

  /// Bind indirect symbols and assign their symbol table indices.
  ///
  /// \param Asm - Assembler providing symbol and section data.
  void bindIndirectSymbols(MCAssembler &Asm);

  /// Compute the symbol table data.
  ///
  /// \param Asm - Assembler providing symbols to classify.
  /// \param LocalSymbolData - [out] Local symbols for the symbol table.
  /// \param ExternalSymbolData - [out] External symbols for the symbol table.
  /// \param UndefinedSymbolData - [out] Undefined symbols for the symbol table.
  void computeSymbolTable(MCAssembler &Asm,
                          std::vector<MachSymbolData> &LocalSymbolData,
                          std::vector<MachSymbolData> &ExternalSymbolData,
                          std::vector<MachSymbolData> &UndefinedSymbolData);

  /// Compute file and virtual addresses for each section.
  ///
  /// \param Asm - Assembler providing section layout.
  void computeSectionAddresses(const MCAssembler &Asm);

  /// Perform late Mach-O symbol binding after layout.
  void executePostLayoutBinding() override;

  /// Check whether the difference between \p SymA and \p FB is fully resolved.
  ///
  /// \param SymA - First symbol in the difference.
  /// \param FB - Fragment defining the second symbol.
  /// \param InSet - True if the difference appears in a set expression.
  /// \param IsPCRel - True if the reference is PC-relative.
  /// \return True if the symbol difference is fully resolved.
  bool isSymbolRefDifferenceFullyResolvedImpl(const MCSymbol &SymA,
                                              const MCFragment &FB, bool InSet,
                                              bool IsPCRel) const override;

  /// Populate the address-significance section from \p Asm.
  ///
  /// \param Asm - Assembler providing symbols that may be address-significant.
  void populateAddrSigSection(MCAssembler &Asm);

  /// Write the Mach-O object file and return the number of bytes written.
  ///
  /// \return The number of bytes written to the output stream.
  uint64_t writeObject() override;
};
} // end namespace llvm

#endif // LLVM_MC_MCMACHOBJECTWRITER_H
