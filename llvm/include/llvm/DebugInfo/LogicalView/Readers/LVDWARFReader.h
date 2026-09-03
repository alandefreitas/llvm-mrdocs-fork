//===-- LVDWARFReader.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVDWARFReader class, which is used to describe a
// debug information (DWARF) reader.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVDWARFREADER_H
#define LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVDWARFREADER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/LogicalView/Readers/LVBinaryReader.h"

namespace llvm {
namespace logicalview {

class LVElement;
class LVLine;
class LVScopeCompileUnit;
class LVSymbol;
class LVType;

/// Alias for a DWARF abbreviation attribute specification.
using AttributeSpec = DWARFAbbreviationDeclaration::AttributeSpec;

/// Reader that builds a logical view from DWARF debug information.
class LLVM_ABI LVDWARFReader final : public LVBinaryReader {
  object::ObjectFile &Obj;

  // Indicates if ranges data are available; in the case of split DWARF any
  // reference to ranges is valid only if the skeleton DIE has been loaded.
  bool RangesDataAvailable = false;
  LVAddress CUBaseAddress = 0;
  LVAddress CUHighAddress = 0;

  LVOffset CurrentEndOffset = 0;

  // In DWARF v4, the files are 1-indexed.
  // In DWARF v5, the files are 0-indexed.
  // The DWARF reader expects the indexes as 1-indexed.
  bool IncrementFileIndex = false;

  // Symbols with locations for current compile unit.
  LVSymbols SymbolsWithLocations;

  // Global Offsets (Offset, Element).
  LVOffsetElementMap GlobalOffsets;

  // Low PC and High PC values for DIE being processed.
  LVAddress CurrentLowPC = 0;
  LVAddress CurrentHighPC = 0;
  bool FoundLowPC = false;
  bool FoundHighPC = false;

  // The value is updated for each Compile Unit that is processed.
  std::optional<LVAddress> TombstoneAddress;

  // Cross references (Elements).
  using LVElementSet = SmallPtrSet<LVElement *, 0>;
  struct LVElementEntry {
    LVElement *Element;
    LVElementSet References;
    LVElementSet Types;
    LVElementEntry(LVElement *Element = nullptr) : Element(Element) {}
  };
  using LVElementReference = DenseMap<LVOffset, LVElementEntry>;
  LVElementReference ElementTable;

  Error loadTargetInfo(const object::ObjectFile &Obj);

  void mapRangeAddress(const object::ObjectFile &Obj) override;

  void traverseDieAndChildren(DWARFDie &DIE, LVScope *Parent,
                              DWARFDie &SkeletonDie);
  // Process the attributes for the given DIE.
  LVScope *processOneDie(const DWARFDie &InputDIE, LVScope *Parent,
                         DWARFDie &SkeletonDie);
  void processOneAttribute(const DWARFDie &Die, LVOffset *OffsetPtr,
                           const AttributeSpec &AttrSpec);
  void createLineAndFileRecords(const DWARFDebugLine::LineTable *Lines);
  void processLocationGaps();

  // Add offset to global map.
  void addGlobalOffset(LVOffset Offset) {
    if (GlobalOffsets.find(Offset) == GlobalOffsets.end())
      // Just associate the DIE offset with a null element, as we do not
      // know if the referenced element has been created.
      GlobalOffsets.emplace(Offset, nullptr);
  }

  // Remove offset from global map.
  void removeGlobalOffset(LVOffset Offset) { GlobalOffsets.erase(Offset); }

  // Get the location information for DW_AT_data_member_location.
  void processLocationMember(dwarf::Attribute Attr,
                             const DWARFFormValue &FormValue,
                             const DWARFDie &Die, uint64_t OffsetOnEntry);
  void processLocationList(dwarf::Attribute Attr,
                           const DWARFFormValue &FormValue, const DWARFDie &Die,
                           uint64_t OffsetOnEntry,
                           bool CallSiteLocation = false);
  void updateReference(dwarf::Attribute Attr, const DWARFFormValue &FormValue);

  // Get an element given the DIE offset.
  LVElement *getElementForOffset(LVOffset offset, LVElement *Element,
                                 bool IsType);

protected:
  /// Build the logical scopes tree from the DWARF input.
  /// \returns Success or an error if scopes could not be built.
  Error createScopes() override;
  /// Sort the root scope after scopes have been created.
  void sortScopes() override;

public:
  /// Deleted; a DWARF reader requires an input object file.
  LVDWARFReader() = delete;
  /// Construct a reader for an object file with DWARF debug info.
  ///
  /// \param Filename Path of the input file being read.
  /// \param FileFormatName Human-readable object format name.
  /// \param Obj Object file that supplies DWARF sections.
  /// \param W Printer used for diagnostic and dump output.
  LVDWARFReader(StringRef Filename, StringRef FileFormatName,
                object::ObjectFile &Obj, ScopedPrinter &W)
      : LVBinaryReader(Filename, FileFormatName, W, LVBinaryType::ELF),
        Obj(Obj) {}
  /// Deleted; DWARF readers are not copyable.
  /// \param Other Unused source reader.
  LVDWARFReader(const LVDWARFReader &Other) = delete;
  /// Deleted; DWARF readers are not copy-assignable.
  /// \param Other Unused source reader.
  LVDWARFReader &operator=(const LVDWARFReader &Other) = delete;
  /// Destroy the DWARF reader.
  ~LVDWARFReader() override = default;

  /// Return the compile-unit base address.
  /// \returns Compile-unit base address.
  LVAddress getCUBaseAddress() const { return CUBaseAddress; }
  /// Set the compile-unit base address to \p Address.
  /// \param Address Compile-unit base address to store.
  void setCUBaseAddress(LVAddress Address) { CUBaseAddress = Address; }
  /// Return the compile-unit high address.
  /// \returns Compile-unit high address.
  LVAddress getCUHighAddress() const { return CUHighAddress; }
  /// Set the compile-unit high address to \p Address.
  /// \param Address Compile-unit high address to store.
  void setCUHighAddress(LVAddress Address) { CUHighAddress = Address; }

  /// Set the tombstone address used for the current compile unit.
  /// \param Address Tombstone address value to store.
  void setTombstoneAddress(LVAddress Address) { TombstoneAddress = Address; }
  /// Return the tombstone address for the current compile unit.
  /// \returns Tombstone address for the current compile unit.
  LVAddress getTombstoneAddress() const {
    assert(TombstoneAddress && "Unset tombstone value");
    return TombstoneAddress.value();
  }

  /// Return the symbols that have location information in the current CU.
  /// \returns Symbols that have location information in the current CU.
  const LVSymbols &GetSymbolsWithLocations() const {
    return SymbolsWithLocations;
  }

  /// Return the register name for a DWARF location opcode and operands.
  /// \param Opcode Location operation code selecting the register encoding.
  /// \param Operands Operand list used to resolve the register name.
  /// \returns Formatted register name for the given opcode and operands.
  std::string getRegisterName(LVSmall Opcode,
                              ArrayRef<uint64_t> Operands) override;

  /// Print a debug summary of this reader to \p OS.
  /// \param OS Output stream to write to.
  void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this reader to the debug stream.
  void dump() const { print(dbgs()); }
#endif
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVDWARFREADER_H
