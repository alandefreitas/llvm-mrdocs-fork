//===- DWARFContext.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===/

#ifndef LLVM_DEBUGINFO_DWARF_DWARFCONTEXT_H
#define LLVM_DEBUGINFO_DWARF_DWARFCONTEXT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFObject.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Host.h"
#include <cstdint>
#include <memory>
#include <mutex>

namespace llvm {

class MemoryBuffer;
class AppleAcceleratorTable;
class DWARFCompileUnit;
class DWARFDebugAbbrev;
class DWARFDebugAranges;
class DWARFDebugFrame;
class DWARFDebugLoc;
class DWARFDebugMacro;
class DWARFDebugNames;
class DWARFGdbIndex;
class DWARFTypeUnit;
class DWARFUnitIndex;

/// DWARFContext
/// This data structure is the top level entity that deals with dwarf debug
/// information parsing. The actual data is supplied through DWARFObj.
class LLVM_ABI DWARFContext : public DIContext {
public:
  /// Holds DWARFContext member state that must be protected under threading.
  ///
  /// This structure contains all member variables for DWARFContext that need
  /// to be protected in multi-threaded environments. Threading support can be
  /// enabled by setting ThreadSafe to true when constructing a DWARFContext to
  /// allow it to be used in a multi-threaded environment, or left disabled to
  /// allow for maximum performance in single-threaded environments.
  class DWARFContextState {
  protected:
    /// Helper enum to distinguish between macro[.dwo] and macinfo[.dwo]
    /// section.
    enum MacroSecType {
      MacinfoSection,    ///< The non-split \c .debug_macinfo section.
      MacinfoDwoSection, ///< The split \c .debug_macinfo.dwo section.
      MacroSection,      ///< The non-split \c .debug_macro section.
      MacroDwoSection    ///< The split \c .debug_macro.dwo section.
    };

    /// Owning DWARFContext whose sections and handlers this state serves.
    DWARFContext &D;
  public:
    /// Construct state bound to owning context \p DC.
    ///
    /// \param DC DWARFContext that owns this state object.
    DWARFContextState(DWARFContext &DC) : D(DC) {}
    /// Virtual destructor for polymorphic state implementations.
    virtual ~DWARFContextState() = default;
    /// Return the lazily populated vector of normal compile and type units.
    ///
    /// \returns Reference to the normal unit vector.
    virtual DWARFUnitVector &getNormalUnits() = 0;
    /// Return the vector of DWO compile and type units.
    ///
    /// \param Lazy If true, set up to parse DWO units but do not parse yet.
    /// \returns Reference to the DWO unit vector.
    virtual DWARFUnitVector &getDWOUnits(bool Lazy = false) = 0;
    /// Get a pointer to the parsed dwo abbreviations object.
    ///
    /// \returns Pointer to the parsed DWO abbreviations, or nullptr if absent.
    virtual const DWARFDebugAbbrev *getDebugAbbrevDWO() = 0;
    /// Return the parsed DWARF compile-unit index (\c .debug_cu_index).
    ///
    /// \returns Reference to the parsed compile-unit index.
    virtual const DWARFUnitIndex &getCUIndex() = 0;
    /// Return the parsed DWARF type-unit index (\c .debug_tu_index).
    ///
    /// \returns Reference to the parsed type-unit index.
    virtual const DWARFUnitIndex &getTUIndex() = 0;
    /// Return the parsed GDB index (\c .gdb_index).
    ///
    /// \returns Reference to the parsed GDB index.
    virtual DWARFGdbIndex &getGdbIndex() = 0;
    /// Return the parsed abbreviations from \c .debug_abbrev.
    ///
    /// \returns Pointer to the parsed abbreviations, or nullptr if absent.
    virtual const DWARFDebugAbbrev *getDebugAbbrev() = 0;
    /// Return the parsed location lists from \c .debug_loc.
    ///
    /// \returns Pointer to the parsed location lists, or nullptr if absent.
    virtual const DWARFDebugLoc *getDebugLoc() = 0;
    /// Return the parsed address ranges from \c .debug_aranges.
    ///
    /// \returns Pointer to the parsed address ranges, or nullptr if absent.
    virtual const DWARFDebugAranges *getDebugAranges() = 0;
    /// Return the parsed line table for compile unit \p U.
    ///
    /// \param U Compile unit whose line table is requested.
    /// \param RecoverableErrHandler Handler for recoverable line-table errors.
    /// \returns The line table on success, or an Error on failure.
    virtual Expected<const DWARFDebugLine::LineTable *>
        getLineTableForUnit(DWARFUnit *U,
                            function_ref<void(Error)> RecoverableErrHandler) = 0;
    /// Clear the cached line table for compile unit \p U.
    ///
    /// \param U Compile unit whose cached line table should be discarded.
    virtual void clearLineTableForUnit(DWARFUnit *U) = 0;
    /// Return the parsed \c .debug_frame call-frame information.
    ///
    /// \returns The parsed frame info on success, or an Error on failure.
    virtual Expected<const DWARFDebugFrame *> getDebugFrame() = 0;
    /// Return the parsed \c .eh_frame call-frame information.
    ///
    /// \returns The parsed EH frame info on success, or an Error on failure.
    virtual Expected<const DWARFDebugFrame *> getEHFrame() = 0;
    /// Return the parsed \c .debug_macinfo section.
    ///
    /// \returns Pointer to the parsed macinfo data, or nullptr if absent.
    virtual const DWARFDebugMacro *getDebugMacinfo() = 0;
    /// Return the parsed \c .debug_macinfo.dwo section for split DWARF objects.
    ///
    /// \returns Pointer to the parsed DWO macinfo data, or nullptr if absent.
    virtual const DWARFDebugMacro *getDebugMacinfoDWO() = 0;
    /// Return the parsed \c .debug_macro section.
    ///
    /// \returns Pointer to the parsed macro data, or nullptr if absent.
    virtual const DWARFDebugMacro *getDebugMacro() = 0;
    /// Return the parsed \c .debug_macro.dwo section for split DWARF objects.
    ///
    /// \returns Pointer to the parsed DWO macro data, or nullptr if absent.
    virtual const DWARFDebugMacro *getDebugMacroDWO() = 0;
    /// Return the parsed DWARF v5 accelerator table (\c .debug_names).
    ///
    /// \returns Reference to the parsed \c .debug_names table.
    virtual const DWARFDebugNames &getDebugNames() = 0;
    /// Return the parsed Apple names accelerator table.
    ///
    /// \returns Reference to the parsed Apple names table.
    virtual const AppleAcceleratorTable &getAppleNames() = 0;
    /// Return the parsed Apple types accelerator table.
    ///
    /// \returns Reference to the parsed Apple types table.
    virtual const AppleAcceleratorTable &getAppleTypes() = 0;
    /// Return the parsed Apple namespaces accelerator table.
    ///
    /// \returns Reference to the parsed Apple namespaces table.
    virtual const AppleAcceleratorTable &getAppleNamespaces() = 0;
    /// Return the parsed Apple Objective-C accelerator table.
    ///
    /// \returns Reference to the parsed Apple Objective-C table.
    virtual const AppleAcceleratorTable &getAppleObjC() = 0;
    /// Return a shared DWARFContext for the DWO/DWP at \p AbsolutePath.
    ///
    /// \param AbsolutePath Absolute path to the DWO or DWP file.
    /// \returns Shared context for that DWO/DWP, or an empty pointer on failure.
    virtual std::shared_ptr<DWARFContext>
        getDWOContext(StringRef AbsolutePath) = 0;
    /// Return the map from type signature hash to parsed type unit.
    ///
    /// \param IsDWO If true, return the DWO type-unit map; otherwise normal.
    /// \returns Const reference to the type-signature-to-unit map.
    virtual const DenseMap<uint64_t, DWARFTypeUnit *> &
    getTypeUnitMap(bool IsDWO) = 0;
    /// True if this state implementation is safe for concurrent use.
    ///
    /// \returns True if the state may be used concurrently; false otherwise.
    virtual bool isThreadSafe() const = 0;

    /// Parse a macro[.dwo] or macinfo[.dwo] section.
    ///
    /// \param SectionType Which macro or macinfo section variant to parse.
    /// \returns Owned parsed macro/macinfo data for the selected section.
    LLVM_ABI std::unique_ptr<DWARFDebugMacro>
    parseMacroOrMacinfo(MacroSecType SectionType);
  };
  friend class DWARFContextState;

private:
  /// All important state for a DWARFContext that needs to be threadsafe needs
  /// to go into DWARFContextState.
  std::unique_ptr<DWARFContextState> State;

  /// The maximum DWARF version of all units.
  unsigned MaxVersion = 0;

  std::function<void(Error)> RecoverableErrorHandler =
      WithColor::defaultErrorHandler;
  std::function<void(Error)> WarningHandler = WithColor::defaultWarningHandler;

  /// Read compile units from the debug_info.dwo section (if necessary)
  /// and type units from the debug_types.dwo section (if necessary)
  /// and store them in DWOUnits.
  /// If \p Lazy is true, set up to parse but don't actually parse them.
  enum { EagerParse = false, LazyParse = true };
  DWARFUnitVector &getDWOUnits(bool Lazy = false);

  std::unique_ptr<const DWARFObject> DObj;

  // When set parses debug_info.dwo/debug_abbrev.dwo manually and populates CU
  // Index, and TU Index for DWARF5.
  bool ParseCUTUIndexManually = false;

public:
  /// Construct a DWARF context over \p DObj and optional DWP/handler settings.
  ///
  /// \param DObj Owned DWARF object supplying section data.
  /// \param DWPName Optional path to a DWP package file.
  /// \param RecoverableErrorHandler Handler for recoverable parse errors.
  /// \param WarningHandler Handler for non-fatal parse warnings.
  /// \param ThreadSafe If true, use a thread-safe state implementation.
  DWARFContext(std::unique_ptr<const DWARFObject> DObj,
               std::string DWPName = "",
               std::function<void(Error)> RecoverableErrorHandler =
                   WithColor::defaultErrorHandler,
               std::function<void(Error)> WarningHandler =
                   WithColor::defaultWarningHandler,
               bool ThreadSafe = false);
  /// Default destructor; releases parsed DWARF state owned by the context.
  ~DWARFContext() override;

  /// Copy construction is deleted; DWARFContext is move-only in practice.
  ///
  /// \param Other Unused; copy construction is not supported.
  DWARFContext(DWARFContext &Other) = delete;
  /// Copy assignment is deleted; DWARFContext is move-only in practice.
  ///
  /// \param Other Unused; copy assignment is not supported.
  /// \returns Never returns; copy assignment is deleted.
  DWARFContext &operator=(DWARFContext &Other) = delete;

  /// Return the underlying DWARF object this context interprets.
  ///
  /// \returns Const reference to the DWARF object supplying section data.
  const DWARFObject &getDWARFObj() const { return *DObj; }

  /// True if \p DICtx is a DWARFContext (kind \c CK_DWARF).
  ///
  /// \param DICtx Debug-info context to test.
  /// \returns True if \p DICtx is a DWARFContext; false otherwise.
  static bool classof(const DIContext *DICtx) {
    return DICtx->getKind() == CK_DWARF;
  }

  /// Dump a textual representation to \p OS. If any \p DumpOffsets are present,
  /// dump only the record at the specified offset.
  ///
  /// \param OS Stream that receives the dump.
  /// \param DumpOpts Options controlling which sections and detail to dump.
  /// \param DumpOffsets Optional per-section offsets; when set, dump only that
  /// record.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts,
            std::array<std::optional<uint64_t>, DIDT_ID_Count> DumpOffsets);

  /// Dump debug information from this context to \p OS using \p DumpOpts.
  ///
  /// \param OS Stream that receives the dump.
  /// \param DumpOpts Options controlling which sections and detail to dump.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) override {
    std::array<std::optional<uint64_t>, DIDT_ID_Count> DumpOffsets;
    dump(OS, DumpOpts, DumpOffsets);
  }

  /// Verify DWARF in this context, writing diagnostics to \p OS.
  ///
  /// \param OS Stream that receives verification diagnostics.
  /// \param DumpOpts Options controlling verification detail.
  /// \returns true if verification succeeds.
  bool verify(raw_ostream &OS, DIDumpOptions DumpOpts = {}) override;

  /// Iterator range over DWARF units (compile and/or type units).
  using unit_iterator_range = DWARFUnitVector::iterator_range;
  /// Iterator range filtered to compile units only.
  using compile_unit_range = DWARFUnitVector::compile_unit_range;

  /// Get units from .debug_info in this context.
  ///
  /// \returns Iterator range over units in the \c .debug_info section.
  unit_iterator_range info_section_units() {
    DWARFUnitVector &NormalUnits = State->getNormalUnits();
    return unit_iterator_range(NormalUnits.begin(),
                               NormalUnits.begin() +
                                   NormalUnits.getNumInfoUnits());
  }

  /// Return the lazily populated vector of compile- and type-units from
  /// \c .debug_info and \c .debug_types.
  ///
  /// \returns Const reference to the normal unit vector.
  const DWARFUnitVector &getNormalUnitsVector() {
    return State->getNormalUnits();
  }

  /// Get units from .debug_types in this context.
  ///
  /// \returns Iterator range over units in the \c .debug_types section.
  unit_iterator_range types_section_units() {
    DWARFUnitVector &NormalUnits = State->getNormalUnits();
    return unit_iterator_range(
        NormalUnits.begin() + NormalUnits.getNumInfoUnits(), NormalUnits.end());
  }

  /// Get compile units in this context.
  ///
  /// \returns Filtered iterator range over compile units in \c .debug_info.
  compile_unit_range compile_units() {
    return make_filter_range(info_section_units(), isCompileUnit);
  }

  // If you want type_units(), it'll need to be a concat iterator of a filter of
  // TUs in info_section + all the (all type) units in types_section

  /// Get all normal compile/type units in this context.
  ///
  /// \returns Iterator range over all normal compile and type units.
  unit_iterator_range normal_units() {
    DWARFUnitVector &NormalUnits = State->getNormalUnits();
    return NormalUnits;
  }

  /// Get units from .debug_info..dwo in the DWO context.
  ///
  /// \returns Iterator range over units in the DWO \c .debug_info section.
  unit_iterator_range dwo_info_section_units() {
    DWARFUnitVector &DWOUnits = State->getDWOUnits();
    return unit_iterator_range(DWOUnits.begin(),
                               DWOUnits.begin() + DWOUnits.getNumInfoUnits());
  }

  /// Return the vector of compile- and type-units from DWO/DWP sections.
  ///
  /// \returns Const reference to the DWO unit vector.
  const DWARFUnitVector &getDWOUnitsVector() {
    return State->getDWOUnits();
  }

  /// Return true of this DWARF context is a DWP file.
  ///
  /// \returns True if this context represents a DWP package file.
  bool isDWP() const;

  /// Get units from .debug_types.dwo in the DWO context.
  ///
  /// \returns Iterator range over units in the DWO \c .debug_types section.
  unit_iterator_range dwo_types_section_units() {
    DWARFUnitVector &DWOUnits = State->getDWOUnits();
    return unit_iterator_range(DWOUnits.begin() + DWOUnits.getNumInfoUnits(),
                               DWOUnits.end());
  }

  /// Get compile units in the DWO context.
  ///
  /// \returns Filtered iterator range over DWO compile units.
  compile_unit_range dwo_compile_units() {
    return make_filter_range(dwo_info_section_units(), isCompileUnit);
  }

  // If you want dwo_type_units(), it'll need to be a concat iterator of a
  // filter of TUs in dwo_info_section + all the (all type) units in
  // dwo_types_section.

  /// Get all units in the DWO context.
  ///
  /// \returns Iterator range over all DWO compile and type units.
  unit_iterator_range dwo_units() {
    DWARFUnitVector &DWOUnits = State->getDWOUnits();
    return DWOUnits;
  }

  /// Get the number of compile units in this context.
  ///
  /// \returns Number of compile units in the normal \c .debug_info section.
  unsigned getNumCompileUnits() {
    return State->getNormalUnits().getNumInfoUnits();
  }

  /// Get the number of type units in this context.
  ///
  /// \returns Number of type units in the normal unit vector.
  unsigned getNumTypeUnits() {
    return State->getNormalUnits().getNumTypesUnits();
  }

  /// Get the number of compile units in the DWO context.
  ///
  /// \returns Number of compile units in the DWO unit vector.
  unsigned getNumDWOCompileUnits() {
    return State->getDWOUnits().getNumInfoUnits();
  }

  /// Get the number of type units in the DWO context.
  ///
  /// \returns Number of type units in the DWO unit vector.
  unsigned getNumDWOTypeUnits() {
    return State->getDWOUnits().getNumTypesUnits();
  }

  /// Get the unit at the specified index.
  ///
  /// \param index Zero-based index into the normal unit vector.
  /// \returns Pointer to the unit at \p index.
  DWARFUnit *getUnitAtIndex(unsigned index) {
    return State->getNormalUnits()[index].get();
  }

  /// Get the unit at the specified index for the DWO units.
  ///
  /// \param index Zero-based index into the DWO unit vector.
  /// \returns Pointer to the DWO unit at \p index.
  DWARFUnit *getDWOUnitAtIndex(unsigned index) {
    return State->getDWOUnits()[index].get();
  }

  /// Return the DWO compile unit whose DWO ID hash is \p Hash, or nullptr.
  ///
  /// \param Hash DWO ID / unit signature hash to look up.
  /// \returns Matching DWO compile unit, or nullptr if none is found.
  DWARFCompileUnit *getDWOCompileUnitForHash(uint64_t Hash);
  /// Return the type unit whose type signature is \p Hash, or nullptr.
  ///
  /// \param Hash Type signature hash to look up.
  /// \param IsDWO If true, search DWO type units; otherwise normal units.
  /// \returns Matching type unit, or nullptr if none is found.
  DWARFTypeUnit *getTypeUnitForHash(uint64_t Hash, bool IsDWO);

  /// Return the DWARF unit that includes an offset (relative to .debug_info).
  ///
  /// \param Offset Byte offset relative to \c .debug_info / \c .debug_types.
  /// \returns The unit containing \p Offset, or nullptr if none matches.
  DWARFUnit *getUnitForOffset(uint64_t Offset);

  /// Return the compile unit that includes an offset (relative to .debug_info).
  ///
  /// \param Offset Byte offset relative to \c .debug_info.
  /// \returns The compile unit containing \p Offset, or nullptr if none matches.
  DWARFCompileUnit *getCompileUnitForOffset(uint64_t Offset);

  /// Get a DIE given an exact offset.
  ///
  /// \param Offset Exact DIE offset within the info/types section.
  /// \returns The DIE at \p Offset, or an invalid DIE if not found.
  DWARFDie getDIEForOffset(uint64_t Offset);

  /// Return the highest DWARF version seen among parsed normal units.
  ///
  /// \returns Highest DWARF version among parsed normal units.
  unsigned getMaxVersion() {
    // Ensure info units have been parsed to discover MaxVersion
    info_section_units();
    return MaxVersion;
  }

  /// Return the highest DWARF version recorded in this context after parsing
  /// compile units from \c .debug_info.dwo.
  ///
  /// \returns Highest DWARF version among parsed DWO units.
  unsigned getMaxDWOVersion() {
    // Ensure DWO info units have been parsed to discover MaxVersion
    dwo_info_section_units();
    return MaxVersion;
  }

  /// Raise the recorded maximum DWARF version if \p Version is greater.
  ///
  /// \param Version Candidate DWARF version from a newly parsed unit.
  void setMaxVersionIfGreater(unsigned Version) {
    if (Version > MaxVersion)
      MaxVersion = Version;
  }

  /// Return the parsed DWARF compile-unit index (\c .debug_cu_index).
  ///
  /// \returns Reference to the parsed compile-unit index.
  const DWARFUnitIndex &getCUIndex();
  /// Return the parsed GDB index (\c .gdb_index).
  ///
  /// \returns Reference to the parsed GDB index.
  DWARFGdbIndex &getGdbIndex();
  /// Get a reference to the parsed DWARF type unit index (\c .debug_tu_index).
  ///
  /// \returns Reference to the parsed type-unit index.
  const DWARFUnitIndex &getTUIndex();

  /// Get a pointer to the parsed DebugAbbrev object.
  ///
  /// \returns Pointer to the parsed abbreviations, or nullptr if absent.
  const DWARFDebugAbbrev *getDebugAbbrev();

  /// Get a pointer to the parsed DebugLoc object.
  ///
  /// \returns Pointer to the parsed location lists, or nullptr if absent.
  const DWARFDebugLoc *getDebugLoc();

  /// Get a pointer to the parsed dwo abbreviations object.
  ///
  /// \returns Pointer to the parsed DWO abbreviations, or nullptr if absent.
  const DWARFDebugAbbrev *getDebugAbbrevDWO();

  /// Get a pointer to the parsed DebugAranges object.
  ///
  /// \returns Pointer to the parsed address ranges, or nullptr if absent.
  const DWARFDebugAranges *getDebugAranges();

  /// Get a pointer to the parsed frame information object.
  ///
  /// \returns The parsed \c .debug_frame info on success, or an Error on failure.
  Expected<const DWARFDebugFrame *> getDebugFrame();

  /// Get a pointer to the parsed eh frame information object.
  ///
  /// \returns The parsed \c .eh_frame info on success, or an Error on failure.
  Expected<const DWARFDebugFrame *> getEHFrame();

  /// Get a pointer to the parsed DebugMacinfo information object.
  ///
  /// \returns Pointer to the parsed macinfo data, or nullptr if absent.
  const DWARFDebugMacro *getDebugMacinfo();

  /// Get a pointer to the parsed DebugMacinfoDWO information object.
  ///
  /// \returns Pointer to the parsed DWO macinfo data, or nullptr if absent.
  const DWARFDebugMacro *getDebugMacinfoDWO();

  /// Get a pointer to the parsed DebugMacro information object.
  ///
  /// \returns Pointer to the parsed macro data, or nullptr if absent.
  const DWARFDebugMacro *getDebugMacro();

  /// Get a pointer to the parsed DebugMacroDWO information object.
  ///
  /// \returns Pointer to the parsed DWO macro data, or nullptr if absent.
  const DWARFDebugMacro *getDebugMacroDWO();

  /// Get a reference to the parsed accelerator table object.
  ///
  /// \returns Reference to the parsed \c .debug_names table.
  const DWARFDebugNames &getDebugNames();

  /// Get a reference to the parsed accelerator table object.
  ///
  /// \returns Reference to the parsed Apple names accelerator table.
  const AppleAcceleratorTable &getAppleNames();

  /// Get a reference to the parsed accelerator table object.
  ///
  /// \returns Reference to the parsed Apple types accelerator table.
  const AppleAcceleratorTable &getAppleTypes();

  /// Get a reference to the parsed accelerator table object.
  ///
  /// \returns Reference to the parsed Apple namespaces accelerator table.
  const AppleAcceleratorTable &getAppleNamespaces();

  /// Get a reference to the parsed accelerator table object.
  ///
  /// \returns Reference to the parsed Apple Objective-C accelerator table.
  const AppleAcceleratorTable &getAppleObjC();

  /// Get a pointer to a parsed line table corresponding to a compile unit.
  /// Report any parsing issues as warnings on stderr.
  ///
  /// \param U Compile unit whose line table is requested.
  /// \returns Pointer to the line table, or nullptr if unavailable.
  const DWARFDebugLine::LineTable *getLineTableForUnit(DWARFUnit *U);

  /// Get a pointer to a parsed line table corresponding to a compile unit.
  /// Report any recoverable parsing problems using the handler.
  ///
  /// \param U Compile unit whose line table is requested.
  /// \param RecoverableErrorHandler Handler for recoverable line-table errors.
  /// \returns The line table on success, or an Error on failure.
  Expected<const DWARFDebugLine::LineTable *>
  getLineTableForUnit(DWARFUnit *U,
                      function_ref<void(Error)> RecoverableErrorHandler);

  /// Clear the cached line table for compile unit \p U.
  ///
  /// When the line table is referred to again, it will be re-populated.
  ///
  /// \param U Compile unit whose cached line table should be discarded.
  void clearLineTableForUnit(DWARFUnit *U);

  /// Return a DataExtractor for the \c .debug_str section.
  ///
  /// \returns DataExtractor over the \c .debug_str section contents.
  DataExtractor getStringExtractor() const {
    return DataExtractor(DObj->getStrSection(), false);
  }
  /// Return a DataExtractor for the \c .debug_str.dwo section.
  ///
  /// \returns DataExtractor over the \c .debug_str.dwo section contents.
  DataExtractor getStringDWOExtractor() const {
    return DataExtractor(DObj->getStrDWOSection(), false);
  }
  /// Return a DataExtractor for the \c .debug_line_str section.
  ///
  /// \returns DataExtractor over the \c .debug_line_str section contents.
  DataExtractor getLineStringExtractor() const {
    return DataExtractor(DObj->getLineStrSection(), false);
  }

  /// Wraps the returned DIEs for a given address.
  struct DIEsForAddress {
    /// Compile unit containing the address, or nullptr if none was found.
    DWARFCompileUnit *CompileUnit = nullptr;
    /// DIE for the function containing the address, if found.
    DWARFDie FunctionDIE;
    /// DIE for the innermost lexical block containing the address, if found.
    DWARFDie BlockDIE;
    /// True if a compile unit was found for the address.
    ///
    /// \returns True if \c CompileUnit is non-null; false otherwise.
    explicit operator bool() const { return CompileUnit != nullptr; }
  };

  /// Return the CU, function DIE, and lexical block DIE for an address.
  ///
  /// TODO: change input parameter from "uint64_t Address"
  ///       into "SectionedAddress Address"
  /// \param Address Absolute address to look up.
  /// \param[in] CheckDWO If this is false then only search for address matches
  ///            in the current context's DIEs. If this is true, then each
  ///            DWARFUnit that has a DWO file will have the debug info in the
  ///            DWO file searched as well. This allows for lookups to succeed
  ///            by searching the split DWARF debug info when using the main
  ///            executable's debug info.
  /// \returns Structure holding the CU, function DIE, and block DIE if found.
  DIEsForAddress getDIEsForAddress(uint64_t Address, bool CheckDWO = false);

  /// Return source line information for the code address \p Address.
  ///
  /// \param Address Sectioned code address to look up.
  /// \param Specifier Controls which file/function fields to fill.
  /// \returns Line info for \p Address, or std::nullopt if unavailable.
  std::optional<DILineInfo> getLineInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;
  /// Return source line information for the data address \p Address.
  ///
  /// \param Address Sectioned data address to look up.
  /// \returns Line info for \p Address, or std::nullopt if unavailable.
  std::optional<DILineInfo>
  getLineInfoForDataAddress(object::SectionedAddress Address) override;
  /// Return line info for each address in the range [\p Address, \p Address+\p Size).
  ///
  /// \param Address Sectioned start address of the range.
  /// \param Size Length in bytes of the address range.
  /// \param Specifier Controls which file/function fields to fill.
  /// \returns Table of line info entries covering the address range.
  DILineInfoTable getLineInfoForAddressRange(
      object::SectionedAddress Address, uint64_t Size,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;
  /// Return the inlining stack (caller frames) for \p Address.
  ///
  /// \param Address Sectioned code address to look up.
  /// \param Specifier Controls which file/function fields to fill.
  /// \returns Inlining stack for \p Address.
  DIInliningInfo getInliningInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;

  /// Return local variables visible at the code address \p Address.
  ///
  /// \param Address Sectioned code address to look up.
  /// \returns Vector of locals visible at \p Address.
  std::vector<DILocal>
  getLocalsForAddress(object::SectionedAddress Address) override;

  /// True if the underlying DWARF object is little-endian.
  ///
  /// \returns True if the DWARF object is little-endian; false otherwise.
  bool isLittleEndian() const { return DObj->isLittleEndian(); }
  /// Highest DWARF version number this context knows how to parse.
  ///
  /// \returns The maximum supported DWARF version (currently 6).
  static unsigned getMaxSupportedVersion() { return 6; }
  /// True if \p version is between 2 and getMaxSupportedVersion() inclusive.
  ///
  /// \param version DWARF version number to test.
  /// \returns True if \p version is supported; false otherwise.
  static bool isSupportedVersion(unsigned version) {
    return version >= 2 && version <= getMaxSupportedVersion();
  }

  /// Return the address sizes (in bytes) this context supports: 2, 4, and 8.
  ///
  /// \returns SmallVector containing the supported address sizes in bytes.
  static SmallVector<uint8_t, 3> getSupportedAddressSizes() {
    return {2, 4, 8};
  }
  /// True if \p AddressSize is one of getSupportedAddressSizes().
  ///
  /// \param AddressSize Address size in bytes to test.
  /// \returns True if \p AddressSize is supported; false otherwise.
  static bool isAddressSizeSupported(unsigned AddressSize) {
    return llvm::is_contained(getSupportedAddressSizes(), AddressSize);
  }
  /// Return success if \p AddressSize is supported, else a formatted error.
  ///
  /// \param AddressSize Address size in bytes to validate.
  /// \param EC Error code used when constructing a failure StringError.
  /// \param Fmt printf-style format string prefix for the error message.
  /// \param Vals Values interpolated into \p Fmt before the size detail.
  /// \returns Error::success() if supported; otherwise a StringError.
  template <typename... Ts>
  static Error checkAddressSizeSupported(unsigned AddressSize,
                                         std::error_code EC, char const *Fmt,
                                         const Ts &...Vals) {
    if (isAddressSizeSupported(AddressSize))
      return Error::success();
    std::string Buffer;
    raw_string_ostream Stream(Buffer);
    Stream << format(Fmt, Vals...)
           << " has unsupported address size: " << AddressSize
           << " (supported are ";
    ListSeparator LS;
    for (unsigned Size : DWARFContext::getSupportedAddressSizes())
      Stream << LS << Size;
    Stream << ')';
    return make_error<StringError>(Buffer, EC);
  }

  /// Return a shared DWARFContext for the DWO/DWP at \p AbsolutePath.
  ///
  /// \param AbsolutePath Absolute path to the DWO or DWP file.
  /// \returns Shared context for that DWO/DWP, or an empty pointer on failure.
  std::shared_ptr<DWARFContext> getDWOContext(StringRef AbsolutePath);

  /// Return the handler used for recoverable parse errors.
  ///
  /// \returns Function reference to the recoverable-error handler.
  function_ref<void(Error)> getRecoverableErrorHandler() {
    return RecoverableErrorHandler;
  }

  /// Return the handler used for non-fatal parse warnings.
  ///
  /// \returns Function reference to the warning handler.
  function_ref<void(Error)> getWarningHandler() { return WarningHandler; }

  /// Whether create() should apply or ignore debug-info relocations.
  enum class ProcessDebugRelocations {
    Process, ///< Apply debug-info relocations when loading the object.
    Ignore   ///< Leave relocation targets unresolved.
  };

  /// Create a DWARFContext from object file \p Obj.
  ///
  /// \param Obj Object file whose debug sections will be parsed.
  /// \param RelocAction Whether to apply or ignore debug relocations.
  /// \param L Optional loaded-object info for relocated addresses.
  /// \param DWPName Optional path to a DWP package file.
  /// \param RecoverableErrorHandler Handler for recoverable parse errors.
  /// \param WarningHandler Handler for non-fatal parse warnings.
  /// \param ThreadSafe If true, use a thread-safe state implementation.
  /// \returns Owned DWARFContext interpreting \p Obj.
  static std::unique_ptr<DWARFContext>
  create(const object::ObjectFile &Obj,
         ProcessDebugRelocations RelocAction = ProcessDebugRelocations::Process,
         const LoadedObjectInfo *L = nullptr, std::string DWPName = "",
         std::function<void(Error)> RecoverableErrorHandler =
             WithColor::defaultErrorHandler,
         std::function<void(Error)> WarningHandler =
             WithColor::defaultWarningHandler,
         bool ThreadSafe = false);

  /// Create a DWARFContext from a map of named section buffers.
  ///
  /// \param Sections Map from section name to owned section contents.
  /// \param AddrSize Address size in bytes for the DWARF data.
  /// \param isLittleEndian True if the DWARF data is little-endian.
  /// \param RecoverableErrorHandler Handler for recoverable parse errors.
  /// \param WarningHandler Handler for non-fatal parse warnings.
  /// \param ThreadSafe If true, use a thread-safe state implementation.
  /// \returns Owned DWARFContext built from \p Sections.
  static std::unique_ptr<DWARFContext>
  create(const StringMap<std::unique_ptr<MemoryBuffer>> &Sections,
         uint8_t AddrSize, bool isLittleEndian = sys::IsLittleEndianHost,
         std::function<void(Error)> RecoverableErrorHandler =
             WithColor::defaultErrorHandler,
         std::function<void(Error)> WarningHandler =
             WithColor::defaultWarningHandler,
         bool ThreadSafe = false);

  /// Get address size from CUs.
  /// TODO: refactor compile_units() to make this const.
  ///
  /// \returns Address size in bytes taken from the compile units.
  uint8_t getCUAddrSize();

  /// Return the target architecture of the object file providing this DWARF.
  ///
  /// \returns Architecture type of the underlying object file.
  Triple::ArchType getArch() const {
    return getDWARFObj().getFile()->getArch();
  }

  /// Return the compile unit which contains instruction with provided
  /// address.
  /// TODO: change input parameter from "uint64_t Address"
  ///       into "SectionedAddress Address"
  ///
  /// \param Address Absolute code address to look up.
  /// \returns Compile unit containing \p Address, or nullptr if none matches.
  DWARFCompileUnit *getCompileUnitForCodeAddress(uint64_t Address);

  /// Return the compile unit that contains data at the given address.
  ///
  /// Note: This is more expensive than `getCompileUnitForAddress`, as if
  /// `Address` isn't found in the CU ranges (which is cheap), then it falls
  /// back to an expensive O(n) walk of all CU's looking for data that spans the
  /// address.
  /// TODO: change input parameter from "uint64_t Address" into
  ///       "SectionedAddress Address"
  ///
  /// \param Address Absolute data address to look up.
  /// \returns Compile unit containing data at \p Address, or nullptr if none.
  DWARFCompileUnit *getCompileUnitForDataAddress(uint64_t Address);

  /// Returns whether CU/TU should be populated manually. TU Index populated
  /// manually only for DWARF5.
  ///
  /// \returns True if CU/TU index contents are parsed manually.
  bool getParseCUTUIndexManually() const { return ParseCUTUIndexManually; }

  /// Sets whether CU/TU should be populated manually. TU Index populated
  /// manually only for DWARF5.
  ///
  /// \param PCUTU If true, parse CU/TU index contents manually for DWARF5.
  void setParseCUTUIndexManually(bool PCUTU) { ParseCUTUIndexManually = PCUTU; }

private:
  void addLocalsForDie(DWARFCompileUnit *CU, DWARFDie Subprogram, DWARFDie Die,
                       std::vector<DILocal> &Result);
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFCONTEXT_H
