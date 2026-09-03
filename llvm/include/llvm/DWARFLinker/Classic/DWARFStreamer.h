//===- DwarfStreamer.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_CLASSIC_DWARFSTREAMER_H
#define LLVM_DWARFLINKER_CLASSIC_DWARFSTREAMER_H

#include "DWARFLinker.h"
#include "llvm/BinaryFormat/Swift.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
template <typename DataT> class AccelTable;

class MCCodeEmitter;
class DWARFDebugMacro;

namespace dwarf_linker {
namespace classic {

///   User of DwarfStreamer should call initialization code
///   for AsmPrinter:
///
///   InitializeAllTargetInfos();
///   InitializeAllTargetMCs();
///   InitializeAllTargets();
///   InitializeAllAsmPrinters();

/// The Dwarf streaming logic.
///
/// All interactions with the MC layer that is used to build the debug
/// information binary representation are handled in this class.
class LLVM_ABI DwarfStreamer : public DwarfEmitter {
public:
  /// Construct a streamer that writes linked DWARF to \p OutFile.
  /// \param OutFileType Kind of output file to produce.
  /// \param OutFile Stream that receives the linked object or assembly.
  /// \param Warning Handler invoked for non-fatal streaming warnings.
  DwarfStreamer(DWARFLinkerBase::OutputFileType OutFileType,
                raw_pwrite_stream &OutFile,
                DWARFLinkerBase::MessageHandlerTy Warning)
      : OutFile(OutFile), OutFileType(OutFileType), WarningHandler(Warning) {}
  /// Destroy this DwarfStreamer.
  ~DwarfStreamer() override = default;

  /// Create and initialize a DwarfStreamer for \p TheTriple.
  /// \param TheTriple Target triple that selects the MC backend.
  /// \param FileType Kind of output file to produce.
  /// \param OutFile Stream that receives the linked object or assembly.
  /// \param Warning Handler invoked for non-fatal streaming warnings.
  /// \returns A new DwarfStreamer, or an Error if creation failed.
  static Expected<std::unique_ptr<DwarfStreamer>> createStreamer(
      const Triple &TheTriple, DWARFLinkerBase::OutputFileType FileType,
      raw_pwrite_stream &OutFile, DWARFLinkerBase::MessageHandlerTy Warning);

  /// Initialize MC layer objects for \p TheTriple.
  /// \param TheTriple Target triple that selects the MC backend.
  /// \param Swift5ReflectionSegmentName Mach-O segment name for Swift
  /// reflection sections.
  /// \returns Success, or an Error if MC initialization failed.
  Error init(Triple TheTriple, StringRef Swift5ReflectionSegmentName);

  /// Dump the file to the disk.
  void finish() override;

  /// Return the AsmPrinter used to emit DWARF.
  /// \returns Reference to the AsmPrinter used for DWARF emission.
  AsmPrinter &getAsmPrinter() const { return *Asm; }

  /// Set the current output section to .debug_info and change
  /// the MC Dwarf version to \p DwarfVersion.
  /// \param DwarfVersion DWARF version to set on the MC layer.
  void switchToDebugInfoSection(unsigned DwarfVersion);

  /// Emit the compilation unit header for \p Unit in the
  /// .debug_info section.
  ///
  /// As a side effect, this also switches the current Dwarf version
  /// of the MC layer to the one of U.getOrigUnit().
  /// \param Unit Compile unit whose header is emitted.
  /// \param DwarfVersion DWARF version to use for the header.
  void emitCompileUnitHeader(CompileUnit &Unit, unsigned DwarfVersion) override;

  /// Recursively emit the DIE tree rooted at \p Die.
  /// \param Die Root DIE of the tree to emit into .debug_info.
  void emitDIE(DIE &Die) override;

  /// Emit the abbreviation table \p Abbrevs to the .debug_abbrev section.
  /// \param Abbrevs Abbreviations to emit.
  /// \param DwarfVersion DWARF version of the output.
  void emitAbbrevs(const std::vector<std::unique_ptr<DIEAbbrev>> &Abbrevs,
                   unsigned DwarfVersion) override;

  /// Emit contents of section SecName From Obj.
  /// \param SecData Bytes to write into the section.
  /// \param SecKind Which debug section to emit into.
  void emitSectionContents(StringRef SecData,
                           DebugSectionKind SecKind) override;

  /// Emit the string table described by \p Pool into .debug_str table.
  /// \param Pool String pool whose contents are written to .debug_str.
  void emitStrings(const NonRelocatableStringpool &Pool) override;

  /// Emit the debug string offset table described by \p StringOffsets into the
  /// .debug_str_offsets table.
  /// \param StringOffset Offsets into .debug_str to emit.
  /// \param TargetDWARFVersion DWARF version that controls the table format.
  void emitStringOffsets(const SmallVector<uint64_t> &StringOffset,
                         uint16_t TargetDWARFVersion) override;

  /// Emit the string table described by \p Pool into .debug_line_str table.
  /// \param Pool String pool whose contents are written to .debug_line_str.
  void emitLineStrings(const NonRelocatableStringpool &Pool) override;

  /// Emit the swift_ast section stored in \p Buffer.
  /// \param Buffer Raw Swift AST section bytes to emit.
  void emitSwiftAST(StringRef Buffer);

  /// Emit the swift reflection section stored in \p Buffer.
  /// \param ReflSectionKind Which Swift5 reflection section to emit.
  /// \param Buffer Raw section bytes to emit.
  /// \param Alignment Required alignment of the section contents.
  /// \param Size Size in bytes of the section contents.
  void emitSwiftReflectionSection(
      llvm::binaryformat::Swift5ReflectionSectionKind ReflSectionKind,
      StringRef Buffer, uint32_t Alignment, uint32_t Size);

  /// Emit debug ranges (.debug_ranges, .debug_rnglists) header.
  /// \param Unit Compile unit the range list belongs to.
  /// \returns Label marking the end of the range list header.
  MCSymbol *emitDwarfDebugRangeListHeader(const CompileUnit &Unit) override;

  /// Emit debug ranges (.debug_ranges, .debug_rnglists) fragment.
  /// \param Unit Compile unit the range list belongs to.
  /// \param LinkedRanges Relocated address ranges to emit.
  /// \param Patch Location that must be updated with the fragment offset.
  /// \param AddrPool Pool of addresses referenced by rnglists.
  /// \returns Success, or an Error if the fragment could not be emitted.
  Error emitDwarfDebugRangeListFragment(const CompileUnit &Unit,
                                        const AddressRanges &LinkedRanges,
                                        PatchLocation Patch,
                                        DebugDieValuePool &AddrPool) override;

  /// Emit debug ranges (.debug_ranges, .debug_rnglists) footer.
  /// \param Unit Compile unit the range list belongs to.
  /// \param EndLabel Label marking the end of the range list.
  void emitDwarfDebugRangeListFooter(const CompileUnit &Unit,
                                     MCSymbol *EndLabel) override;

  /// Emit debug locations (.debug_loc, .debug_loclists) header.
  /// \param Unit Compile unit the location list belongs to.
  /// \returns Label marking the end of the location list header.
  MCSymbol *emitDwarfDebugLocListHeader(const CompileUnit &Unit) override;

  /// Emit .debug_addr header.
  /// \param Unit Compile unit the address table belongs to.
  /// \returns Label marking the end of the address table header.
  MCSymbol *emitDwarfDebugAddrsHeader(const CompileUnit &Unit) override;

  /// Emit the addresses described by \p Addrs into .debug_addr table.
  /// \param Addrs Addresses to write.
  /// \param AddrSize Size in bytes of each address entry.
  void emitDwarfDebugAddrs(const SmallVector<uint64_t> &Addrs,
                           uint8_t AddrSize) override;

  /// Emit .debug_addr footer.
  /// \param Unit Compile unit the address table belongs to.
  /// \param EndLabel Label marking the end of the address table.
  void emitDwarfDebugAddrsFooter(const CompileUnit &Unit,
                                 MCSymbol *EndLabel) override;

  /// Emit debug locations (.debug_loc, .debug_loclists) fragment.
  /// \param Unit Compile unit the location list belongs to.
  /// \param LinkedLocationExpression Relocated location expressions to emit.
  /// \param Patch Location that must be updated with the fragment offset.
  /// \param AddrPool Pool of addresses referenced by loclists.
  /// \returns Success, or an Error if the fragment could not be emitted.
  Error emitDwarfDebugLocListFragment(
      const CompileUnit &Unit,
      const DWARFLocationExpressionsVector &LinkedLocationExpression,
      PatchLocation Patch, DebugDieValuePool &AddrPool) override;

  /// Emit debug locations (.debug_loc, .debug_loclists) footer.
  /// \param Unit Compile unit the location list belongs to.
  /// \param EndLabel Label marking the end of the location list.
  void emitDwarfDebugLocListFooter(const CompileUnit &Unit,
                                   MCSymbol *EndLabel) override;

  /// Emit .debug_aranges entries for \p Unit.
  /// \param Unit Compile unit whose address ranges are emitted.
  /// \param LinkedRanges Relocated address ranges to emit.
  void emitDwarfDebugArangesTable(const CompileUnit &Unit,
                                  const AddressRanges &LinkedRanges) override;

  /// Returns size of generated .debug_ranges section.
  /// \returns Size in bytes of the generated .debug_ranges section.
  uint64_t getRangesSectionSize() const override { return RangesSectionSize; }

  /// Returns size of generated .debug_rnglists section.
  /// \returns Size in bytes of the generated .debug_rnglists section.
  uint64_t getRngListsSectionSize() const override {
    return RngListsSectionSize;
  }

  /// Emit \p LineTable into the .debug_line section for \p Unit.
  ///
  /// The optional parameter \p RowOffsets, if provided, will be populated with
  /// the offsets of each line table row in the output .debug_line section.
  /// \param LineTable Line table to emit.
  /// \param Unit Compile unit the line table belongs to.
  /// \param DebugStrPool Pool for .debug_str strings referenced by the table.
  /// \param DebugLineStrPool Pool for .debug_line_str strings.
  /// \param RowOffsets Optional output of per-row offsets in .debug_line.
  void
  emitLineTableForUnit(const DWARFDebugLine::LineTable &LineTable,
                       const CompileUnit &Unit, OffsetsStringPool &DebugStrPool,
                       OffsetsStringPool &DebugLineStrPool,
                       std::vector<uint64_t> *RowOffsets = nullptr) override;

  /// Returns size of generated .debug_line section.
  /// \returns Size in bytes of the generated .debug_line section.
  uint64_t getLineSectionSize() const override { return LineSectionSize; }

  /// Emit the .debug_pubnames contribution for \p Unit.
  /// \param Unit Compile unit whose public names are emitted.
  void emitPubNamesForUnit(const CompileUnit &Unit) override;

  /// Emit the .debug_pubtypes contribution for \p Unit.
  /// \param Unit Compile unit whose public types are emitted.
  void emitPubTypesForUnit(const CompileUnit &Unit) override;

  /// Emit a CIE.
  /// \param CIEBytes Raw CIE bytes to emit into .debug_frame.
  void emitCIE(StringRef CIEBytes) override;

  /// Emit an FDE with data \p Bytes.
  /// \param CIEOffset Offset of the CIE this FDE refers to.
  /// \param AddreSize Size in bytes of addresses in the FDE.
  /// \param Address Initial location covered by the FDE.
  /// \param Bytes Raw FDE payload to emit.
  void emitFDE(uint32_t CIEOffset, uint32_t AddreSize, uint64_t Address,
               StringRef Bytes) override;

  /// Emit DWARF debug names.
  /// \param Table Accelerator table to emit as .debug_names.
  void emitDebugNames(DWARF5AccelTable &Table) override;

  /// Emit Apple namespaces accelerator table.
  /// \param Table Accelerator table to emit as .apple_namespaces.
  void emitAppleNamespaces(
      AccelTable<AppleAccelTableStaticOffsetData> &Table) override;

  /// Emit Apple names accelerator table.
  /// \param Table Accelerator table to emit as .apple_names.
  void
  emitAppleNames(AccelTable<AppleAccelTableStaticOffsetData> &Table) override;

  /// Emit Apple Objective-C accelerator table.
  /// \param Table Accelerator table to emit as .apple_objc.
  void
  emitAppleObjc(AccelTable<AppleAccelTableStaticOffsetData> &Table) override;

  /// Emit Apple type accelerator table.
  /// \param Table Accelerator table to emit as .apple_types.
  void
  emitAppleTypes(AccelTable<AppleAccelTableStaticTypeData> &Table) override;

  /// Returns size of generated .debug_frame section.
  /// \returns Size in bytes of the generated .debug_frame section.
  uint64_t getFrameSectionSize() const override { return FrameSectionSize; }

  /// Returns size of generated .debug_info section.
  /// \returns Size in bytes of the generated .debug_info section.
  uint64_t getDebugInfoSectionSize() const override {
    return DebugInfoSectionSize;
  }

  /// Returns size of generated .debug_macinfo section.
  /// \returns Size in bytes of the generated .debug_macinfo section.
  uint64_t getDebugMacInfoSectionSize() const override {
    return MacInfoSectionSize;
  }

  /// Returns size of generated .debug_macro section.
  /// \returns Size in bytes of the generated .debug_macro section.
  uint64_t getDebugMacroSectionSize() const override {
    return MacroSectionSize;
  }

  /// Returns size of generated .debug_loclists section.
  /// \returns Size in bytes of the generated .debug_loclists section.
  uint64_t getLocListsSectionSize() const override {
    return LocListsSectionSize;
  }

  /// Returns size of generated .debug_addr section.
  /// \returns Size in bytes of the generated .debug_addr section.
  uint64_t getDebugAddrSectionSize() const override { return AddrSectionSize; }

  /// Emit DWARFv4 and DWARFv5 macro tables for \p Context.
  ///
  /// Use \p UnitMacroMap to get the compilation unit by macro table offset.
  /// Side effects: fills \p StringPool with macro strings and updates
  /// DW_AT_macro_info / DW_AT_macros attributes for corresponding compile
  /// units.
  /// \param Context DWARF context providing the macro tables to emit.
  /// \param UnitMacroMap Map from macro table offset to compile unit.
  /// \param StringPool String pool that receives macro strings.
  void emitMacroTables(DWARFContext *Context,
                       const Offset2UnitMap &UnitMacroMap,
                       OffsetsStringPool &StringPool) override;

private:
  inline void warn(const Twine &Warning, StringRef Context = "") {
    if (WarningHandler)
      WarningHandler(Warning, Context, nullptr);
  }

  Expected<uint64_t> clampSecOffset(uint64_t Offset, dwarf::FormParams FP,
                                    StringRef Section) {
    if (Offset <= FP.getDwarfMaxOffset())
      return Offset;
    return createStringError(Section + " section offset 0x" +
                             Twine::utohexstr(Offset) + " exceeds the " +
                             dwarf::FormatString(FP.Format) + " limit");
  }

  MCSection *getMCSection(DebugSectionKind SecKind);

  void emitMacroTableImpl(const DWARFDebugMacro *MacroTable,
                          const Offset2UnitMap &UnitMacroMap,
                          OffsetsStringPool &StringPool, uint64_t &OutOffset);

  /// Emit piece of .debug_ranges for \p LinkedRanges.
  Error emitDwarfDebugRangesTableFragment(const CompileUnit &Unit,
                                          const AddressRanges &LinkedRanges,
                                          PatchLocation Patch);

  /// Emit piece of .debug_rnglists for \p LinkedRanges.
  Error emitDwarfDebugRngListsTableFragment(const CompileUnit &Unit,
                                            const AddressRanges &LinkedRanges,
                                            PatchLocation Patch,
                                            DebugDieValuePool &AddrPool);

  /// Emit piece of .debug_loc for \p LinkedRanges.
  Error emitDwarfDebugLocTableFragment(
      const CompileUnit &Unit,
      const DWARFLocationExpressionsVector &LinkedLocationExpression,
      PatchLocation Patch);

  /// Emit piece of .debug_loclists for \p LinkedRanges.
  Error emitDwarfDebugLocListsTableFragment(
      const CompileUnit &Unit,
      const DWARFLocationExpressionsVector &LinkedLocationExpression,
      PatchLocation Patch, DebugDieValuePool &AddrPool);

  /// \defgroup Line table emission
  /// @{
  void emitLineTablePrologue(const DWARFDebugLine::Prologue &P,
                             OffsetsStringPool &DebugStrPool,
                             OffsetsStringPool &DebugLineStrPool);
  void emitLineTableString(const DWARFDebugLine::Prologue &P,
                           const DWARFFormValue &String,
                           OffsetsStringPool &DebugStrPool,
                           OffsetsStringPool &DebugLineStrPool);
  void emitLineTableProloguePayload(const DWARFDebugLine::Prologue &P,
                                    OffsetsStringPool &DebugStrPool,
                                    OffsetsStringPool &DebugLineStrPool);
  void emitLineTablePrologueV2IncludeAndFileTable(
      const DWARFDebugLine::Prologue &P, OffsetsStringPool &DebugStrPool,
      OffsetsStringPool &DebugLineStrPool);
  void emitLineTablePrologueV5IncludeAndFileTable(
      const DWARFDebugLine::Prologue &P, OffsetsStringPool &DebugStrPool,
      OffsetsStringPool &DebugLineStrPool);
  void emitLineTableRows(const DWARFDebugLine::LineTable &LineTable,
                         MCSymbol *LineEndSym, unsigned AddressByteSize,
                         std::vector<uint64_t> *RowOffsets = nullptr);
  void emitIntOffset(uint64_t Offset, dwarf::DwarfFormat Format,
                     uint64_t &SectionSize);
  void emitLabelDifference(const MCSymbol *Hi, const MCSymbol *Lo,
                           dwarf::DwarfFormat Format, uint64_t &SectionSize);
  /// @}

  /// \defgroup MCObjects MC layer objects constructed by the streamer
  /// @{
  MCTargetOptions MCOptions;
  std::unique_ptr<MCRegisterInfo> MRI;
  std::unique_ptr<MCAsmInfo> MAI;
  std::unique_ptr<MCObjectFileInfo> MOFI;
  std::unique_ptr<MCContext> MC;
  MCAsmBackend *MAB; // Owned by MCStreamer
  std::unique_ptr<MCInstrInfo> MII;
  std::unique_ptr<MCSubtargetInfo> MSTI;
  MCCodeEmitter *MCE; // Owned by MCStreamer
  MCStreamer *MS;     // Owned by AsmPrinter
  std::unique_ptr<TargetMachine> TM;
  std::unique_ptr<AsmPrinter> Asm;
  /// @}

  /// The output file we stream the linked Dwarf to.
  raw_pwrite_stream &OutFile;
  DWARFLinker::OutputFileType OutFileType = DWARFLinker::OutputFileType::Object;

  uint64_t RangesSectionSize = 0;
  uint64_t RngListsSectionSize = 0;
  uint64_t LocSectionSize = 0;
  uint64_t LocListsSectionSize = 0;
  uint64_t LineSectionSize = 0;
  uint64_t FrameSectionSize = 0;
  uint64_t DebugInfoSectionSize = 0;
  uint64_t MacInfoSectionSize = 0;
  uint64_t MacroSectionSize = 0;
  uint64_t AddrSectionSize = 0;
  uint64_t StrOffsetSectionSize = 0;

  /// Keep track of emitted CUs and their Unique ID.
  struct EmittedUnit {
    unsigned ID;
    MCSymbol *LabelBegin;
  };
  std::vector<EmittedUnit> EmittedUnits;

  /// Emit the pubnames or pubtypes section contribution for \p
  /// Unit into \p Sec. The data is provided in \p Names.
  void emitPubSectionForUnit(MCSection *Sec, StringRef Name,
                             const CompileUnit &Unit,
                             const std::vector<CompileUnit::AccelInfo> &Names);

  DWARFLinkerBase::MessageHandlerTy WarningHandler = nullptr;
};

} // end of namespace classic
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_DWARFLINKER_CLASSIC_DWARFSTREAMER_H
