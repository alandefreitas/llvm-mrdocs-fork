//===- DWARFLinkerCompileUnit.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_CLASSIC_DWARFLINKERCOMPILEUNIT_H
#define LLVM_DWARFLINKER_CLASSIC_DWARFLINKERCOMPILEUNIT_H

#include "llvm/ADT/AddressRanges.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {
namespace dwarf_linker {
namespace classic {

class DeclContext;

/// Mapped value in the address map is the offset to apply to the
/// linked address.
using RangesTy = AddressRangesMap;

/// Location of a DIE attribute value that may need patching after linking.
///
/// This structure keeps a patch for the attribute and, optionally, the value
/// of a relocation which should be applied. Currently, only location
/// attributes need a relocation: either to the function ranges if the location
/// attribute is of type 'loclist', or to the operand of DW_OP_addr/DW_OP_addrx
/// if the location attribute is of type 'exprloc'.
///
/// ASSUMPTION: Location attributes of 'loclist' type containing 'exprloc'
/// with address expression operands are not supported yet.
struct PatchLocation {
  /// Iterator pointing at the DIE attribute value to patch.
  DIE::value_iterator I;

  /// Optional relocation adjustment applied when patching this attribute.
  int64_t RelocAdjustment = 0;

  /// Construct an empty patch location.
  PatchLocation() = default;

  /// Construct a patch location for the attribute value at \p I.
  /// \param I Iterator pointing at the DIE attribute value to patch.
  PatchLocation(DIE::value_iterator I) : I(I) {}

  /// Construct a patch location with a relocation adjustment.
  /// \param I Iterator pointing at the DIE attribute value to patch.
  /// \param Reloc Relocation adjustment applied when patching this attribute.
  PatchLocation(DIE::value_iterator I, int64_t Reloc)
      : I(I), RelocAdjustment(Reloc) {}

  /// Replace the integer attribute value at this patch location.
  /// \param New New integer value to store in the attribute.
  void set(uint64_t New) const {
    assert(I);
    const auto &Old = *I;
    assert(Old.getType() == DIEValue::isInteger);
    *I = DIEValue(Old.getAttribute(), Old.getForm(), DIEInteger(New));
  }

  /// Return the current integer attribute value at this patch location.
  /// \returns The current integer attribute value at this patch location.
  uint64_t get() const {
    assert(I);
    return I->getDIEInteger().getValue();
  }
};

/// Patch locations for range-list attributes in a compile unit.
using RngListAttributesTy = SmallVector<PatchLocation>;

/// Patch locations for location-list attributes in a compile unit.
using LocListAttributesTy = SmallVector<PatchLocation>;

/// Patch locations for DW_AT_LLVM_stmt_sequence attributes in a compile unit.
using StmtSeqListAttributesTy = SmallVector<PatchLocation>;

/// Stores all information relating to a compile unit, be it in its original
/// instance in the object file to its brand new cloned and generated DIE tree.
class CompileUnit {
public:
  /// Information gathered about a DIE in the object file.
  struct DIEInfo {
    /// Address offset to apply to the described entity.
    int64_t AddrAdjust;

    /// ODR Declaration context.
    DeclContext *Ctxt;

    /// Cloned version of that DIE.
    DIE *Clone;

    /// The index of this DIE's parent.
    uint32_t ParentIdx;

    /// Is the DIE part of the linked output?
    bool Keep : 1;

    /// Was this DIE's entity found in the map?
    bool InDebugMap : 1;

    /// Is this a pure forward declaration we can strip?
    bool Prune : 1;

    /// Does DIE transitively refer an incomplete decl?
    bool Incomplete : 1;

    /// Is DIE in the clang module scope?
    bool InModuleScope : 1;

    /// Is ODR marking done?
    bool ODRMarkingDone : 1;

    /// Is this a reference to a DIE that hasn't been cloned yet?
    bool UnclonedReference : 1;

    /// Is this a variable with a location attribute referencing address?
    bool HasLocationExpressionAddr : 1;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    /// Dump this DIE info to the debug stream.
    LLVM_DUMP_METHOD void dump();
#endif // if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  };

  /// Construct a compile unit wrapper around \p OrigUnit.
  /// \param OrigUnit Original DWARF unit from the input object.
  /// \param ID Unique identifier assigned to this compile unit.
  /// \param CanUseODR Whether ODR uniquing is allowed for this unit's language.
  /// \param ClangModuleName Name of the Clang module, or empty if not a module.
  CompileUnit(DWARFUnit &OrigUnit, unsigned ID, bool CanUseODR,
              StringRef ClangModuleName)
      : OrigUnit(OrigUnit), ID(ID), ClangModuleName(ClangModuleName) {
    Info.resize(OrigUnit.getNumDIEs());

    auto CUDie = OrigUnit.getUnitDIE(false);
    if (!CUDie) {
      HasODR = false;
      return;
    }
    if (auto Lang = CUDie.getLanguage())
      HasODR = CanUseODR && (*Lang == dwarf::DW_LANG_C_plus_plus ||
                             *Lang == dwarf::DW_LANG_C_plus_plus_03 ||
                             *Lang == dwarf::DW_LANG_C_plus_plus_11 ||
                             *Lang == dwarf::DW_LANG_C_plus_plus_14 ||
                             *Lang == dwarf::DW_LANG_ObjC_plus_plus);
    else
      HasODR = false;
  }

  /// Return the original DWARF unit from the input object.
  /// \returns The original DWARF unit from the input object.
  DWARFUnit &getOrigUnit() const { return OrigUnit; }

  /// Return the unique identifier assigned to this compile unit.
  /// \returns The unique identifier assigned to this compile unit.
  unsigned getUniqueID() const { return ID; }

  /// Create the output DIE unit that will hold the cloned DIE tree.
  void createOutputDIE() {
    NewUnit.emplace(OrigUnit.getUnitDIE().getTag());

    // Propogate the section offset so that DIEntry can compute
    // correct absolute offsets for DW_FORM_ref_addr references
    NewUnit->setDebugSectionOffset(StartOffset);
  }

  /// Return the root DIE of the cloned output unit, or nullptr if none exists.
  /// \returns The root DIE of the cloned output unit, or nullptr if none exists.
  DIE *getOutputUnitDIE() const {
    if (NewUnit)
      return &const_cast<BasicDIEUnit &>(*NewUnit).getUnitDie();
    return nullptr;
  }

  /// Return the DWARF tag of this compile unit.
  /// \returns The DWARF tag of this compile unit.
  dwarf::Tag getTag() const { return OrigUnit.getUnitDIE().getTag(); }

  /// Return whether this unit is subject to the ODR rule.
  /// \returns true if this unit is subject to the ODR rule.
  bool hasODR() const { return HasODR; }

  /// Return whether this unit comes from a Clang module.
  /// \returns true if this unit comes from a Clang module.
  bool isClangModule() const { return !ClangModuleName.empty(); }

  /// Return the DW_AT_language of this compile unit.
  /// \returns The DW_AT_language of this compile unit.
  LLVM_ABI uint16_t getLanguage();

  /// Return the DW_AT_LLVM_sysroot of the compile unit or an empty StringRef.
  /// \returns The DW_AT_LLVM_sysroot of the compile unit, or an empty StringRef.
  LLVM_ABI StringRef getSysRoot();

  /// Return the Clang module name, or an empty string if this is not a module.
  /// \returns The Clang module name, or an empty string if this is not a module.
  const std::string &getClangModuleName() const { return ClangModuleName; }

  /// Return mutable DIE info for the DIE at index \p Idx.
  /// \param Idx Index of the DIE in the original unit.
  /// \returns Mutable DIE info for the DIE at index \p Idx.
  DIEInfo &getInfo(unsigned Idx) { return Info[Idx]; }

  /// Return DIE info for the DIE at index \p Idx.
  /// \param Idx Index of the DIE in the original unit.
  /// \returns DIE info for the DIE at index \p Idx.
  const DIEInfo &getInfo(unsigned Idx) const { return Info[Idx]; }

  /// Return mutable DIE info for \p Die.
  /// \param Die DIE whose info should be returned.
  /// \returns Mutable DIE info for \p Die.
  DIEInfo &getInfo(const DWARFDie &Die) {
    unsigned Idx = getOrigUnit().getDIEIndex(Die);
    return Info[Idx];
  }

  /// Return the start offset of this unit in the output .debug_info section.
  /// \returns The start offset of this unit in the output .debug_info section.
  uint64_t getStartOffset() const { return StartOffset; }

  /// Return the end offset of this unit in the output .debug_info section.
  /// \returns The end offset of this unit in the output .debug_info section.
  uint64_t getNextUnitOffset() const { return NextUnitOffset; }

  /// Set the start offset of this unit in the output .debug_info section.
  /// \param DebugInfoSize Absolute offset where this unit begins.
  void setStartOffset(uint64_t DebugInfoSize) {
    StartOffset = DebugInfoSize;
    if (NewUnit)
      NewUnit->setDebugSectionOffset(DebugInfoSize);
  }

  /// Return the low PC of this unit, if known.
  /// \returns The low PC of this unit, or std::nullopt if unknown.
  std::optional<uint64_t> getLowPc() const { return LowPc; }

  /// Return the high PC of this unit.
  /// \returns The high PC of this unit.
  uint64_t getHighPc() const { return HighPc; }

  /// Return whether a label was recorded at address \p Addr.
  /// \param Addr Address to look up among recorded labels.
  /// \returns true if a label was recorded at \p Addr.
  bool hasLabelAt(uint64_t Addr) const { return Labels.count(Addr); }

  /// Return the relocated PC ranges of functions in this unit.
  /// \returns The relocated PC ranges of functions in this unit.
  const RangesTy &getFunctionRanges() const { return Ranges; }

  /// Return the range-list attributes that still need patching.
  /// \returns The range-list attributes that still need patching.
  const RngListAttributesTy &getRangesAttributes() { return RangeAttributes; }

  /// Return the unit-level DW_AT_ranges patch location, if any.
  /// \returns The unit-level DW_AT_ranges patch location, or std::nullopt.
  std::optional<PatchLocation> getUnitRangesAttribute() const {
    return UnitRangeAttribute;
  }

  /// Return the location-list attributes that still need patching.
  /// \returns The location-list attributes that still need patching.
  const LocListAttributesTy &getLocationAttributes() const {
    return LocationAttributes;
  }

  /// Return the DW_AT_LLVM_stmt_sequence attributes that may need patching.
  /// \returns The DW_AT_LLVM_stmt_sequence attributes that may need patching.
  const StmtSeqListAttributesTy &getStmtSeqListAttributes() const {
    return StmtSeqListAttributes;
  }

  /// Mark every DIE in this unit as kept. This function also
  /// marks variables as InDebugMap so that they appear in the
  /// reconstructed accelerator tables.
  LLVM_ABI void markEverythingAsKept();

  /// Compute the end offset for this unit. Must be called after the CU's DIEs
  /// have been cloned.  \returns the next unit offset (which is also the
  /// current debug_info section size).
  /// \param DwarfVersion DWARF version of the output unit.
  LLVM_ABI uint64_t computeNextUnitOffset(uint16_t DwarfVersion);

  /// Record a forward DIE reference that must be patched later.
  ///
  /// Keep track of a forward reference to DIE \p Die in \p RefUnit by \p
  /// Attr. The attribute should be fixed up later to point to the absolute
  /// offset of \p Die in the debug_info section or to the canonical offset of
  /// \p Ctxt if it is non-null.
  /// \param Die DIE being referenced.
  /// \param RefUnit Compile unit that owns \p Die.
  /// \param Ctxt Optional declaration context whose canonical offset may be
  /// used.
  /// \param Attr Attribute location that will receive the fixed-up offset.
  LLVM_ABI void noteForwardReference(DIE *Die, const CompileUnit *RefUnit,
                                     DeclContext *Ctxt, PatchLocation Attr);

  /// Apply all fixups recorded by noteForwardReference().
  LLVM_ABI void fixupForwardReferences();

  /// Add the low_pc of a label that is relocated by applying
  /// offset \p PCOffset.
  /// \param LabelLowPc Original low_pc of the label.
  /// \param PcOffset Offset applied to obtain the linked address.
  LLVM_ABI void addLabelLowPc(uint64_t LabelLowPc, int64_t PcOffset);

  /// Add a function range [\p LowPC, \p HighPC) that is relocated by applying
  /// offset \p PCOffset.
  /// \param LowPC Inclusive start of the function range.
  /// \param HighPC Exclusive end of the function range.
  /// \param PCOffset Offset applied to obtain the linked addresses.
  LLVM_ABI void addFunctionRange(uint64_t LowPC, uint64_t HighPC,
                                 int64_t PCOffset);

  /// Keep track of a DW_AT_range attribute that we will need to patch up later.
  /// \param Die DIE that owns the range attribute.
  /// \param Attr Patch location of the range attribute value.
  LLVM_ABI void noteRangeAttribute(const DIE &Die, PatchLocation Attr);

  /// Keep track of a location attribute pointing to a location list in the
  /// debug_loc section.
  /// \param Attr Patch location of the location attribute value.
  LLVM_ABI void noteLocationAttribute(PatchLocation Attr);

  /// Record that a DW_AT_LLVM_stmt_sequence attribute may need to be patched.
  /// \param Attr Patch location of the stmt_sequence attribute value.
  LLVM_ABI void noteStmtSeqListAttribute(PatchLocation Attr);

  /// Add a name accelerator entry for \a Die with \a Name.
  /// \param Die DIE described by the accelerator entry.
  /// \param Name Name stored in the accelerator table.
  LLVM_ABI void addNamespaceAccelerator(const DIE *Die,
                                        DwarfStringPoolEntryRef Name);

  /// Add a name accelerator entry for \a Die with \a Name.
  /// \param Die DIE described by the accelerator entry.
  /// \param Name Name stored in the accelerator table.
  /// \param SkipPubnamesSection If true, emit only in apple_* sections.
  LLVM_ABI void addNameAccelerator(const DIE *Die, DwarfStringPoolEntryRef Name,
                                   bool SkipPubnamesSection = false);

  /// Add various accelerator entries for \p Die with \p Name which is stored
  /// in the string table at \p Offset. \p Name must be an Objective-C
  /// selector.
  /// \param Die DIE described by the accelerator entry.
  /// \param Name Objective-C selector name stored in the accelerator table.
  /// \param SkipPubnamesSection If true, emit only in apple_* sections.
  LLVM_ABI void addObjCAccelerator(const DIE *Die, DwarfStringPoolEntryRef Name,
                                   bool SkipPubnamesSection = false);

  /// Add a type accelerator entry for \p Die with \p Name which is stored in
  /// the string table at \p Offset.
  /// \param Die DIE described by the accelerator entry.
  /// \param Name Type name stored in the accelerator table.
  /// \param ObjcClassImplementation Whether this is an ObjC class implementation.
  /// \param QualifiedNameHash Hash of the fully qualified type name.
  LLVM_ABI void addTypeAccelerator(const DIE *Die, DwarfStringPoolEntryRef Name,
                                   bool ObjcClassImplementation,
                                   uint32_t QualifiedNameHash);

  /// Accelerator-table entry describing a name, type, namespace, or ObjC DIE.
  struct AccelInfo {
    /// Name of the entry.
    DwarfStringPoolEntryRef Name;

    /// DIE this entry describes.
    const DIE *Die;

    /// Hash of the fully qualified name.
    uint32_t QualifiedNameHash;

    /// Emit this entry only in the apple_* sections.
    bool SkipPubSection;

    /// Is this an ObjC class implementation?
    bool ObjcClassImplementation;

    /// Construct an accelerator entry for \p Die with \p Name.
    /// \param Name Name stored in the accelerator table.
    /// \param Die DIE described by this entry.
    /// \param SkipPubSection If true, emit only in apple_* sections.
    AccelInfo(DwarfStringPoolEntryRef Name, const DIE *Die,
              bool SkipPubSection = false)
        : Name(Name), Die(Die), SkipPubSection(SkipPubSection) {}

    /// Construct a typed accelerator entry with a qualified-name hash.
    /// \param Name Name stored in the accelerator table.
    /// \param Die DIE described by this entry.
    /// \param QualifiedNameHash Hash of the fully qualified type name.
    /// \param ObjCClassIsImplementation Whether this is an ObjC class
    /// implementation.
    AccelInfo(DwarfStringPoolEntryRef Name, const DIE *Die,
              uint32_t QualifiedNameHash, bool ObjCClassIsImplementation)
        : Name(Name), Die(Die), QualifiedNameHash(QualifiedNameHash),
          SkipPubSection(false),
          ObjcClassImplementation(ObjCClassIsImplementation) {}
  };

  /// Return the public-names accelerator entries for this unit.
  /// \returns The public-names accelerator entries for this unit.
  const std::vector<AccelInfo> &getPubnames() const { return Pubnames; }

  /// Return the public-types accelerator entries for this unit.
  /// \returns The public-types accelerator entries for this unit.
  const std::vector<AccelInfo> &getPubtypes() const { return Pubtypes; }

  /// Return the namespace accelerator entries for this unit.
  /// \returns The namespace accelerator entries for this unit.
  const std::vector<AccelInfo> &getNamespaces() const { return Namespaces; }

  /// Return the Objective-C accelerator entries for this unit.
  /// \returns The Objective-C accelerator entries for this unit.
  const std::vector<AccelInfo> &getObjC() const { return ObjC; }

  /// Return the MCSymbol marking the beginning of this unit's output.
  /// \returns The MCSymbol marking the beginning of this unit's output.
  MCSymbol *getLabelBegin() { return LabelBegin; }

  /// Set the MCSymbol marking the beginning of this unit's output.
  /// \param S Symbol to use as the unit begin label.
  void setLabelBegin(MCSymbol *S) { LabelBegin = S; }

private:
  DWARFUnit &OrigUnit;
  unsigned ID;
  std::vector<DIEInfo> Info; ///< DIE info indexed by DIE index.
  std::optional<BasicDIEUnit> NewUnit;
  MCSymbol *LabelBegin = nullptr;

  uint64_t StartOffset;
  uint64_t NextUnitOffset;

  std::optional<uint64_t> LowPc;
  uint64_t HighPc = 0;

  /// A list of attributes to fixup with the absolute offset of
  /// a DIE in the debug_info section.
  ///
  /// The offsets for the attributes in this array couldn't be set while
  /// cloning because for cross-cu forward references the target DIE's offset
  /// isn't known you emit the reference attribute.
  std::vector<
      std::tuple<DIE *, const CompileUnit *, DeclContext *, PatchLocation>>
      ForwardDIEReferences;

  /// The ranges in that map are the PC ranges for functions in this unit,
  /// associated with the PC offset to apply to the addresses to get
  /// the linked address.
  RangesTy Ranges;

  /// The DW_AT_low_pc of each DW_TAG_label.
  SmallDenseMap<uint64_t, uint64_t, 1> Labels;

  /// 'rnglist'(DW_AT_ranges, DW_AT_start_scope) attributes to patch after
  /// we have gathered all the unit's function addresses.
  /// @{
  RngListAttributesTy RangeAttributes;
  std::optional<PatchLocation> UnitRangeAttribute;
  /// @}

  /// Location attributes that need to be transferred from the
  /// original debug_loc section to the linked one. They are stored
  /// along with the PC offset that is to be applied to their
  /// function's address or to be applied to address operands of
  /// location expression.
  LocListAttributesTy LocationAttributes;

  // List of DW_AT_LLVM_stmt_sequence attributes that may need to be patched
  // after the dwarf linker rewrites the line table. During line table rewrite
  // the line table format might change, so we have to patch any offsets that
  // reference its contents.
  StmtSeqListAttributesTy StmtSeqListAttributes;

  /// Accelerator entries for the unit, both for the pub*
  /// sections and the apple* ones.
  /// @{
  std::vector<AccelInfo> Pubnames;
  std::vector<AccelInfo> Pubtypes;
  std::vector<AccelInfo> Namespaces;
  std::vector<AccelInfo> ObjC;
  /// @}

  /// Is this unit subject to the ODR rule?
  bool HasODR;

  /// The DW_AT_language of this unit.
  uint16_t Language = 0;

  /// The DW_AT_LLVM_sysroot of this unit.
  std::string SysRoot;

  /// If this is a Clang module, this holds the module's name.
  std::string ClangModuleName;
};

} // end of namespace classic
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_DWARFLINKER_CLASSIC_DWARFLINKERCOMPILEUNIT_H
