//===- Wasm.h - Wasm object file implementation -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the WasmObjectFile class, which implements the ObjectFile
// interface for Wasm files.
//
// See: https://github.com/WebAssembly/design/blob/main/BinaryEncoding.md
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_WASM_H
#define LLVM_OBJECT_WASM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace llvm {
namespace object {

/// Symbol from a WebAssembly object file's linking symbol table.
class WasmSymbol {
public:
  /// Construct a WasmSymbol from linking metadata and optional type info.
  ///
  /// \param Info Symbol info from the object's symbol table.
  /// \param GlobalType Global type, or nullptr if not a global symbol.
  /// \param TableType Table type, or nullptr if not a table symbol.
  /// \param Signature Function/tag signature, or nullptr if not applicable.
  WasmSymbol(const wasm::WasmSymbolInfo &Info,
             const wasm::WasmGlobalType *GlobalType,
             const wasm::WasmTableType *TableType,
             const wasm::WasmSignature *Signature)
      : Info(Info), GlobalType(GlobalType), TableType(TableType),
        Signature(Signature) {
    assert(!Signature || Signature->Kind != wasm::WasmSignature::Placeholder);
  }

  /// Symbol info as represented in the symbol's 'syminfo' entry of an object
  /// file's symbol table.
  wasm::WasmSymbolInfo Info;
  /// Type of this symbol if it is a global; otherwise nullptr.
  const wasm::WasmGlobalType *GlobalType;
  /// Type of this symbol if it is a table; otherwise nullptr.
  const wasm::WasmTableType *TableType;
  /// Signature of this symbol if it is a function or tag; otherwise nullptr.
  const wasm::WasmSignature *Signature;

  /// True if this symbol is a function.
  ///
  /// \return True if this symbol is a function.
  bool isTypeFunction() const {
    return Info.Kind == wasm::WASM_SYMBOL_TYPE_FUNCTION;
  }

  /// True if this symbol is a table.
  ///
  /// \return True if this symbol is a table.
  bool isTypeTable() const { return Info.Kind == wasm::WASM_SYMBOL_TYPE_TABLE; }

  /// True if this symbol is a data segment.
  ///
  /// \return True if this symbol is a data segment.
  bool isTypeData() const { return Info.Kind == wasm::WASM_SYMBOL_TYPE_DATA; }

  /// True if this symbol is a global.
  ///
  /// \return True if this symbol is a global.
  bool isTypeGlobal() const {
    return Info.Kind == wasm::WASM_SYMBOL_TYPE_GLOBAL;
  }

  /// True if this symbol refers to a section.
  ///
  /// \return True if this symbol refers to a section.
  bool isTypeSection() const {
    return Info.Kind == wasm::WASM_SYMBOL_TYPE_SECTION;
  }

  /// True if this symbol is a tag.
  ///
  /// \return True if this symbol is a tag.
  bool isTypeTag() const { return Info.Kind == wasm::WASM_SYMBOL_TYPE_TAG; }

  /// True if this symbol is defined in this object.
  ///
  /// \return True if this symbol is defined in this object.
  bool isDefined() const { return !isUndefined(); }

  /// True if this symbol is undefined (imported).
  ///
  /// \return True if this symbol is undefined (imported).
  bool isUndefined() const {
    return (Info.Flags & wasm::WASM_SYMBOL_UNDEFINED) != 0;
  }

  /// True if this symbol has weak binding.
  ///
  /// \return True if this symbol has weak binding.
  bool isBindingWeak() const {
    return getBinding() == wasm::WASM_SYMBOL_BINDING_WEAK;
  }

  /// True if this symbol has global binding.
  ///
  /// \return True if this symbol has global binding.
  bool isBindingGlobal() const {
    return getBinding() == wasm::WASM_SYMBOL_BINDING_GLOBAL;
  }

  /// True if this symbol has local binding.
  ///
  /// \return True if this symbol has local binding.
  bool isBindingLocal() const {
    return getBinding() == wasm::WASM_SYMBOL_BINDING_LOCAL;
  }

  /// Binding flags of this symbol (weak, global, or local).
  ///
  /// \return The binding flags of this symbol.
  unsigned getBinding() const {
    return Info.Flags & wasm::WASM_SYMBOL_BINDING_MASK;
  }

  /// True if this symbol has hidden visibility.
  ///
  /// \return True if this symbol has hidden visibility.
  bool isHidden() const {
    return getVisibility() == wasm::WASM_SYMBOL_VISIBILITY_HIDDEN;
  }

  /// Visibility flags of this symbol.
  ///
  /// \return The visibility flags of this symbol.
  unsigned getVisibility() const {
    return Info.Flags & wasm::WASM_SYMBOL_VISIBILITY_MASK;
  }

  /// Print a human-readable representation of this symbol to \p Out.
  ///
  /// \param Out Stream to write to.
  LLVM_ABI void print(raw_ostream &Out) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this symbol to stderr (debug builds).
  LLVM_DUMP_METHOD void dump() const;
#endif
};

/// A single section within a WebAssembly object file.
struct WasmSection {
  /// Default-construct an empty WasmSection.
  WasmSection() = default;

  /// Section type (core Wasm section id or custom).
  uint32_t Type = 0;
  /// Byte offset of this section within the file.
  uint32_t Offset = 0;
  /// Section name (user-defined/custom sections only).
  StringRef Name;
  /// COMDAT group index from the "comdat info" section, or UINT32_MAX if none.
  uint32_t Comdat = UINT32_MAX;
  /// Raw section payload bytes.
  ArrayRef<uint8_t> Content;
  /// Relocations that apply to this section.
  std::vector<wasm::WasmRelocation> Relocations;
  /// Length of the LEB encoding of the section header's size field.
  std::optional<uint8_t> HeaderSecSizeEncodingLen;
};

/// A data segment and its offset within the data section.
struct WasmSegment {
  /// Offset of this segment within the data section payload.
  uint32_t SectionOffset;
  /// Wasm data segment descriptor and contents.
  wasm::WasmDataSegment Data;
};

/// ObjectFile implementation for WebAssembly binaries.
class LLVM_ABI WasmObjectFile : public ObjectFile {

public:
  /// Parse a Wasm object from \p Object, reporting failures via \p Err.
  ///
  /// \param Object Memory buffer holding the Wasm binary.
  /// \param Err Set on parse failure; must be checked by the caller.
  WasmObjectFile(MemoryBufferRef Object, Error &Err);

  /// Wasm object header (magic and version).
  ///
  /// \return The Wasm object header.
  const wasm::WasmObjectHeader &getHeader() const;
  /// Wasm symbol for opaque handle \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The Wasm symbol for \p Symb.
  const WasmSymbol &getWasmSymbol(const DataRefImpl &Symb) const;
  /// Wasm symbol for SymbolRef \p Symbol.
  ///
  /// \param Symbol Symbol reference in this object.
  /// \return The Wasm symbol for \p Symbol.
  const WasmSymbol &getWasmSymbol(const SymbolRef &Symbol) const;
  /// Wasm section for SectionRef \p Section.
  ///
  /// \param Section Section reference in this object.
  /// \return The Wasm section for \p Section.
  const WasmSection &getWasmSection(const SectionRef &Section) const;
  /// Wasm relocation for RelocationRef \p Ref.
  ///
  /// \param Ref Relocation reference in this object.
  /// \return The Wasm relocation for \p Ref.
  const wasm::WasmRelocation &getWasmRelocation(const RelocationRef &Ref) const;

  /// True if \p v is a WasmObjectFile.
  ///
  /// \param v Binary to test.
  /// \return True if \p v is a WasmObjectFile.
  static bool classof(const Binary *v) { return v->isWasm(); }

  /// Dylink (dynamic linking) metadata from the "dylink" / "dylink.0" section.
  ///
  /// \return The dylink metadata for this object.
  const wasm::WasmDylinkInfo &dylinkInfo() const { return DylinkInfo; }
  /// Producer tool metadata from the "producers" custom section.
  ///
  /// \return The producer metadata for this object.
  const wasm::WasmProducerInfo &getProducerInfo() const { return ProducerInfo; }
  /// Target feature requirements from the "target_features" section.
  ///
  /// \return The target feature entries for this object.
  ArrayRef<wasm::WasmFeatureEntry> getTargetFeatures() const {
    return TargetFeatures;
  }
  /// Type (signature) section entries.
  ///
  /// \return The type section signatures.
  ArrayRef<wasm::WasmSignature> types() const { return Signatures; }
  /// Import section entries.
  ///
  /// \return The import section entries.
  ArrayRef<wasm::WasmImport> imports() const { return Imports; }
  /// Table section entries.
  ///
  /// \return The table section entries.
  ArrayRef<wasm::WasmTable> tables() const { return Tables; }
  /// Memory section limit descriptors.
  ///
  /// \return The memory section limit descriptors.
  ArrayRef<wasm::WasmLimits> memories() const { return Memories; }
  /// Global section entries.
  ///
  /// \return The global section entries.
  ArrayRef<wasm::WasmGlobal> globals() const { return Globals; }
  /// Tag section entries.
  ///
  /// \return The tag section entries.
  ArrayRef<wasm::WasmTag> tags() const { return Tags; }
  /// Export section entries.
  ///
  /// \return The export section entries.
  ArrayRef<wasm::WasmExport> exports() const { return Exports; }
  /// Linking metadata from the "linking" custom section.
  ///
  /// \return The linking metadata for this object.
  const wasm::WasmLinkingData &linkingData() const { return LinkingData; }
  /// Number of symbols in the linking symbol table.
  ///
  /// \return The number of symbols in the linking symbol table.
  uint32_t getNumberOfSymbols() const { return Symbols.size(); }
  /// Element (table initialization) segments.
  ///
  /// \return The element segments.
  ArrayRef<wasm::WasmElemSegment> elements() const { return ElemSegments; }
  /// Data segments with their section offsets.
  ///
  /// \return The data segments with their section offsets.
  ArrayRef<WasmSegment> dataSegments() const { return DataSegments; }
  /// Defined functions from the code section.
  ///
  /// \return The defined functions from the code section.
  ArrayRef<wasm::WasmFunction> functions() const { return Functions; }
  /// Debug names from the "name" custom section.
  ///
  /// \return The debug names from the "name" custom section.
  ArrayRef<wasm::WasmDebugName> debugNames() const { return DebugNames; }
  /// Function index of the start function, or -1 if none.
  ///
  /// \return The start function index, or -1 if none.
  uint32_t startFunction() const { return StartFunction; }
  /// Number of imported globals (before defined globals).
  ///
  /// \return The number of imported globals.
  uint32_t getNumImportedGlobals() const { return NumImportedGlobals; }
  /// Number of imported tables (before defined tables).
  ///
  /// \return The number of imported tables.
  uint32_t getNumImportedTables() const { return NumImportedTables; }
  /// Number of imported functions (before defined functions).
  ///
  /// \return The number of imported functions.
  uint32_t getNumImportedFunctions() const { return NumImportedFunctions; }
  /// Number of imported tags (before defined tags).
  ///
  /// \return The number of imported tags.
  uint32_t getNumImportedTags() const { return NumImportedTags; }
  /// Number of sections in this object.
  ///
  /// \return The number of sections in this object.
  uint32_t getNumSections() const { return Sections.size(); }
  /// Advance \p Symb to the next symbol.
  ///
  /// \param Symb Opaque symbol handle to advance.
  void moveSymbolNext(DataRefImpl &Symb) const override;

  /// Symbol flags for \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The symbol flags, or an error if unavailable.
  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override;

  /// Iterator to the first symbol in this object.
  ///
  /// \return An iterator to the first symbol.
  basic_symbol_iterator symbol_begin() const override;

  /// Past-the-end iterator for symbols in this object.
  ///
  /// \return A past-the-end symbol iterator.
  basic_symbol_iterator symbol_end() const override;
  /// Name of symbol \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The symbol name, or an error if unavailable.
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;

  /// Always false; WasmObjectFile does not represent a 64-bit address space.
  ///
  /// \return Always false.
  bool is64Bit() const override { return false; }

  /// Virtual address of symbol \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The virtual address, or an error if unavailable.
  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  /// Format-specific value of Wasm symbol \p Sym.
  ///
  /// \param Sym Wasm symbol whose value is requested.
  /// \return The format-specific value of \p Sym.
  uint64_t getWasmSymbolValue(const WasmSymbol &Sym) const;
  /// Format-specific symbol value for \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The format-specific value of \p Symb.
  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;
  /// Alignment of symbol \p Symb as the actual value (not log 2).
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The symbol alignment as the actual value (not log 2).
  uint32_t getSymbolAlignment(DataRefImpl Symb) const override;
  /// Size of common symbol \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The size of the common symbol.
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;
  /// Classify symbol \p Symb (data, function, etc.).
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The symbol type, or an error if unavailable.
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const override;
  /// Section defining symbol \p Symb, or section_end() if undefined.
  ///
  /// \param Symb Opaque symbol handle.
  /// \return An iterator to the defining section, or section_end() if undefined.
  Expected<section_iterator> getSymbolSection(DataRefImpl Symb) const override;
  /// Section index of symbol \p Sym.
  ///
  /// \param Sym Symbol whose defining section index is requested.
  /// \return The defining section index of \p Sym.
  uint32_t getSymbolSectionId(SymbolRef Sym) const;
  /// Size in bytes of symbol \p Sym.
  ///
  /// \param Sym Symbol whose size is requested.
  /// \return The size of \p Sym in bytes.
  uint32_t getSymbolSize(SymbolRef Sym) const;

  // Overrides from SectionRef.
  /// Advance \p Sec to the next section.
  ///
  /// \param Sec Opaque section handle to advance.
  void moveSectionNext(DataRefImpl &Sec) const override;
  /// Name of section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \return The section name, or an error if unavailable.
  Expected<StringRef> getSectionName(DataRefImpl Sec) const override;
  /// Virtual load address of section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \return The virtual load address of \p Sec.
  uint64_t getSectionAddress(DataRefImpl Sec) const override;
  /// Index of section \p Sec within this object.
  ///
  /// \param Sec Opaque section handle.
  /// \return The section index of \p Sec.
  uint64_t getSectionIndex(DataRefImpl Sec) const override;
  /// Size in bytes of section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \return The size of \p Sec in bytes.
  uint64_t getSectionSize(DataRefImpl Sec) const override;
  /// Raw contents of section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \return The section contents, or an error if unavailable.
  Expected<ArrayRef<uint8_t>>
  getSectionContents(DataRefImpl Sec) const override;
  /// Alignment of section \p Sec in bytes.
  ///
  /// \param Sec Opaque section handle.
  /// \return The alignment of \p Sec in bytes.
  uint64_t getSectionAlignment(DataRefImpl Sec) const override;
  /// True if section \p Sec is compressed.
  ///
  /// \param Sec Opaque section handle.
  /// \return True if section \p Sec is compressed.
  bool isSectionCompressed(DataRefImpl Sec) const override;
  /// True if section \p Sec contains executable code.
  ///
  /// \param Sec Opaque section handle.
  /// \return True if section \p Sec contains executable code.
  bool isSectionText(DataRefImpl Sec) const override;
  /// True if section \p Sec contains initialized data (not text).
  ///
  /// \param Sec Opaque section handle.
  /// \return True if section \p Sec contains initialized data.
  bool isSectionData(DataRefImpl Sec) const override;
  /// True if section \p Sec is BSS (zero-initialized, no file contents).
  ///
  /// \param Sec Opaque section handle.
  /// \return True if section \p Sec is BSS.
  bool isSectionBSS(DataRefImpl Sec) const override;
  /// True if section \p Sec's contents are absent from the object image.
  ///
  /// \param Sec Opaque section handle.
  /// \return True if section \p Sec is virtual.
  bool isSectionVirtual(DataRefImpl Sec) const override;
  /// Iterator to the first relocation in section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \return An iterator to the first relocation in \p Sec.
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override;
  /// Past-the-end iterator for relocations in section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \return A past-the-end relocation iterator for \p Sec.
  relocation_iterator section_rel_end(DataRefImpl Sec) const override;

  // Overrides from RelocationRef.
  /// Advance \p Rel to the next relocation.
  ///
  /// \param Rel Opaque relocation handle to advance.
  void moveRelocationNext(DataRefImpl &Rel) const override;
  /// Byte offset of relocation \p Rel within its section.
  ///
  /// \param Rel Opaque relocation handle.
  /// \return The byte offset of \p Rel within its section.
  uint64_t getRelocationOffset(DataRefImpl Rel) const override;
  /// Symbol referenced by relocation \p Rel.
  ///
  /// \param Rel Opaque relocation handle.
  /// \return An iterator to the symbol referenced by \p Rel.
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override;
  /// Format-specific type encoding of relocation \p Rel.
  ///
  /// \param Rel Opaque relocation handle.
  /// \return The format-specific type encoding of \p Rel.
  uint64_t getRelocationType(DataRefImpl Rel) const override;
  /// Append a display name for relocation \p Rel's type to \p Result.
  ///
  /// \param Rel Opaque relocation handle.
  /// \param Result Buffer that receives the type name.
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override;

  /// Iterator to the first section in this object.
  ///
  /// \return An iterator to the first section.
  section_iterator section_begin() const override;
  /// Past-the-end iterator for sections in this object.
  ///
  /// \return A past-the-end section iterator.
  section_iterator section_end() const override;
  /// Number of bytes used to represent an address in this format.
  ///
  /// \return The address width in bytes.
  uint8_t getBytesInAddress() const override;
  /// Human-readable name of this object file format.
  ///
  /// \return The format name string.
  StringRef getFileFormatName() const override;
  /// Target architecture of this object file.
  ///
  /// \return The architecture of this object file.
  Triple::ArchType getArch() const override;
  /// Subtarget features described by this object file.
  ///
  /// \return The subtarget features, or an error if unavailable.
  Expected<SubtargetFeatures> getFeatures() const override;
  /// True if this is a relocatable object (has a linking section).
  ///
  /// \return True if this object has a linking section.
  bool isRelocatableObject() const override;
  /// True if this is a shared object (has a dylink section).
  ///
  /// \return True if this object has a dylink section.
  bool isSharedObject() const;
  /// True if the type section contained types this parser does not model.
  ///
  /// \return True if unmodeled types were encountered.
  bool hasUnmodeledTypes() const { return HasUnmodeledTypes; }

  /// Cursor used while parsing a Wasm section payload.
  struct ReadContext {
    /// Start of the current parse region.
    const uint8_t *Start;
    /// Current read position within the region.
    const uint8_t *Ptr;
    /// One-past-the-end of the current parse region.
    const uint8_t *End;
  };

private:
  bool isValidFunctionIndex(uint32_t Index) const;
  bool isDefinedFunctionIndex(uint32_t Index) const;
  bool isValidGlobalIndex(uint32_t Index) const;
  bool isValidTableNumber(uint32_t Index) const;
  bool isDefinedGlobalIndex(uint32_t Index) const;
  bool isDefinedTableNumber(uint32_t Index) const;
  bool isValidTagIndex(uint32_t Index) const;
  bool isDefinedTagIndex(uint32_t Index) const;
  bool isValidFunctionSymbol(uint32_t Index) const;
  bool isValidTableSymbol(uint32_t Index) const;
  bool isValidGlobalSymbol(uint32_t Index) const;
  bool isValidTagSymbol(uint32_t Index) const;
  bool isValidDataSymbol(uint32_t Index) const;
  bool isValidSectionSymbol(uint32_t Index) const;
  wasm::WasmFunction &getDefinedFunction(uint32_t Index);
  const wasm::WasmFunction &getDefinedFunction(uint32_t Index) const;
  const wasm::WasmGlobal &getDefinedGlobal(uint32_t Index) const;
  wasm::WasmTag &getDefinedTag(uint32_t Index);

  const WasmSection &getWasmSection(DataRefImpl Ref) const;
  const wasm::WasmRelocation &getWasmRelocation(DataRefImpl Ref) const;
  uint32_t getSymbolSectionIdImpl(const WasmSymbol &Symb) const;

  Error parseSection(WasmSection &Sec);
  Error parseCustomSection(WasmSection &Sec, ReadContext &Ctx);

  Error parseImport(ReadContext &Ctx, wasm::WasmImport &Im);

  // Standard section types
  Error parseTypeSection(ReadContext &Ctx);
  Error parseImportSection(ReadContext &Ctx);
  Error parseFunctionSection(ReadContext &Ctx);
  Error parseTableSection(ReadContext &Ctx);
  Error parseMemorySection(ReadContext &Ctx);
  Error parseTagSection(ReadContext &Ctx);
  Error parseGlobalSection(ReadContext &Ctx);
  Error parseExportSection(ReadContext &Ctx);
  Error parseStartSection(ReadContext &Ctx);
  Error parseElemSection(ReadContext &Ctx);
  Error parseCodeSection(ReadContext &Ctx);
  Error parseDataSection(ReadContext &Ctx);
  Error parseDataCountSection(ReadContext &Ctx);

  // Custom section types
  Error parseDylinkSection(ReadContext &Ctx);
  Error parseDylink0Section(ReadContext &Ctx);
  Error parseNameSection(ReadContext &Ctx);
  Error parseLinkingSection(ReadContext &Ctx);
  Error parseLinkingSectionSymtab(ReadContext &Ctx);
  Error parseLinkingSectionComdat(ReadContext &Ctx);
  Error parseProducersSection(ReadContext &Ctx);
  Error parseTargetFeaturesSection(ReadContext &Ctx);
  Error parseRelocSection(StringRef Name, ReadContext &Ctx);

  wasm::WasmObjectHeader Header;
  std::vector<WasmSection> Sections;
  wasm::WasmDylinkInfo DylinkInfo;
  wasm::WasmProducerInfo ProducerInfo;
  std::vector<wasm::WasmFeatureEntry> TargetFeatures;
  std::vector<wasm::WasmSignature> Signatures;
  std::vector<wasm::WasmTable> Tables;
  std::vector<wasm::WasmLimits> Memories;
  std::vector<wasm::WasmGlobal> Globals;
  std::vector<wasm::WasmTag> Tags;
  std::vector<wasm::WasmImport> Imports;
  std::vector<wasm::WasmExport> Exports;
  std::vector<wasm::WasmElemSegment> ElemSegments;
  std::vector<WasmSegment> DataSegments;
  std::optional<size_t> DataCount;
  std::vector<wasm::WasmFunction> Functions;
  std::vector<WasmSymbol> Symbols;
  std::vector<wasm::WasmDebugName> DebugNames;
  uint32_t StartFunction = -1;
  bool HasLinkingSection = false;
  bool HasDylinkSection = false;
  bool HasMemory64 = false;
  bool HasUnmodeledTypes = false;
  wasm::WasmLinkingData LinkingData;
  uint32_t NumImportedGlobals = 0;
  uint32_t NumImportedTables = 0;
  uint32_t NumImportedFunctions = 0;
  uint32_t NumImportedTags = 0;
  uint32_t CodeSection = 0;
  uint32_t DataSection = 0;
  uint32_t TagSection = 0;
  uint32_t GlobalSection = 0;
  uint32_t TableSection = 0;
};

/// Helper that validates Wasm section ordering constraints.
class WasmSectionOrderChecker {
public:
  /// Canonical order indices for core and known custom Wasm sections.
  enum : int {
    WASM_SEC_ORDER_NONE = 0, ///< Sentinel; must be zero.

    // Core sections
    WASM_SEC_ORDER_TYPE,      ///< Type section.
    WASM_SEC_ORDER_IMPORT,    ///< Import section.
    WASM_SEC_ORDER_FUNCTION,  ///< Function section.
    WASM_SEC_ORDER_TABLE,     ///< Table section.
    WASM_SEC_ORDER_MEMORY,    ///< Memory section.
    WASM_SEC_ORDER_TAG,       ///< Tag section.
    WASM_SEC_ORDER_GLOBAL,    ///< Global section.
    WASM_SEC_ORDER_EXPORT,    ///< Export section.
    WASM_SEC_ORDER_START,     ///< Start section.
    WASM_SEC_ORDER_ELEM,      ///< Element section.
    WASM_SEC_ORDER_DATACOUNT, ///< Data count section.
    WASM_SEC_ORDER_CODE,      ///< Code section.
    WASM_SEC_ORDER_DATA,      ///< Data section.

    // Custom sections
    /// "dylink" should be the very first section in the module.
    WASM_SEC_ORDER_DYLINK,
    /// "linking" section requires DATA section in order to validate data
    /// symbols.
    WASM_SEC_ORDER_LINKING,
    /// Must come after "linking" section in order to validate reloc indexes.
    WASM_SEC_ORDER_RELOC,
    /// "name" section must appear after DATA. Comes after "linking" to allow
    /// symbol table to set default function name.
    WASM_SEC_ORDER_NAME,
    /// "producers" section must appear after "name" section.
    WASM_SEC_ORDER_PRODUCERS,
    /// "target_features" section must appear after producers section.
    WASM_SEC_ORDER_TARGET_FEATURES,

    WASM_NUM_SEC_ORDERS ///< Count of order indices; must be last.
  };

  /// Sections that may or may not be present, but cannot be predecessors.
  LLVM_ABI static int DisallowedPredecessors[WASM_NUM_SEC_ORDERS]
                                            [WASM_NUM_SEC_ORDERS];

  /// True if section \p ID (and optional custom name) may appear next.
  ///
  /// \param ID Wasm section type id.
  /// \param CustomSectionName Name when \p ID is a custom section.
  /// \return True if the section may appear next in order.
  LLVM_ABI bool isValidSectionOrder(unsigned ID,
                                    StringRef CustomSectionName = "");

private:
  bool Seen[WASM_NUM_SEC_ORDERS] = {}; // Sections that have been seen already

  // Returns -1 for unknown sections.
  int getSectionOrder(unsigned ID, StringRef CustomSectionName = "");
};

} // end namespace object

/// Stream \p Sym to \p OS using WasmSymbol::print.
///
/// \param OS Output stream.
/// \param Sym Symbol to print.
/// \return A reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const object::WasmSymbol &Sym) {
  Sym.print(OS);
  return OS;
}

} // end namespace llvm

#endif // LLVM_OBJECT_WASM_H
