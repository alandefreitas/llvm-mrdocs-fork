//===- MCSymbolWasm.h -  ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCSYMBOLWASM_H
#define LLVM_MC_MCSYMBOLWASM_H

#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolTableEntry.h"

namespace llvm {

/// MCSymbol specialization for WebAssembly object files.
///
/// Tracks Wasm symbol type, weak/hidden/comdat and linking flags, import and
/// export names, function signature, and optional global or table type.
class MCSymbolWasm : public MCSymbol {
  std::optional<wasm::WasmSymbolType> Type;
  bool IsWeak = false;
  bool IsHidden = false;
  bool IsComdat = false;
  bool OmitFromLinkingSection = false;
  mutable bool IsUsedInInitArray = false;
  mutable bool IsUsedInGOT = false;
  std::optional<StringRef> ImportModule;
  std::optional<StringRef> ImportName;
  std::optional<StringRef> ExportName;
  wasm::WasmSignature *Signature = nullptr;
  std::optional<wasm::WasmGlobalType> GlobalType;
  std::optional<wasm::WasmTableType> TableType;

  /// An expression describing how to calculate the size of a symbol. If a
  /// symbol has no size this field will be NULL.
  const MCExpr *SymbolSize = nullptr;

public:
  /// Construct a Wasm MCSymbol.
  ///
  /// \param Name - Name table entry for the symbol.
  /// \param isTemporary - True if this is an assembler temporary label.
  MCSymbolWasm(const MCSymbolTableEntry *Name, bool isTemporary)
      : MCSymbol(Name, isTemporary) {}

  /// Return true if this symbol is visible outside this translation unit.
  ///
  /// \return True if the symbol is external.
  bool isExternal() const { return IsExternal; }
  /// Set whether this symbol is external.
  ///
  /// \param Value - True if the symbol is external.
  void setExternal(bool Value) const { IsExternal = Value; }
  /// Return the expression that computes this symbol's size, or null.
  ///
  /// \return The size expression, or null if the symbol has no size.
  const MCExpr *getSize() const { return SymbolSize; }
  /// Set the expression that computes this symbol's size.
  ///
  /// \param SS - Size expression, or null if the symbol has no size.
  void setSize(const MCExpr *SS) { SymbolSize = SS; }

  /// Return true if this symbol has Wasm function type.
  ///
  /// \return True if the symbol type is function.
  bool isFunction() const { return Type == wasm::WASM_SYMBOL_TYPE_FUNCTION; }
  /// Return true if this symbol has Wasm data type (or no type set).
  ///
  /// Data is the default value if not set.
  /// \return True if the symbol is data or has no type set.
  bool isData() const { return !Type || Type == wasm::WASM_SYMBOL_TYPE_DATA; }
  /// Return true if this symbol has Wasm global type.
  ///
  /// \return True if the symbol type is global.
  bool isGlobal() const { return Type == wasm::WASM_SYMBOL_TYPE_GLOBAL; }
  /// Return true if this symbol has Wasm table type.
  ///
  /// \return True if the symbol type is table.
  bool isTable() const { return Type == wasm::WASM_SYMBOL_TYPE_TABLE; }
  /// Return true if this symbol has Wasm section type.
  ///
  /// \return True if the symbol type is section.
  bool isSection() const { return Type == wasm::WASM_SYMBOL_TYPE_SECTION; }
  /// Return true if this symbol has Wasm tag type.
  ///
  /// \return True if the symbol type is tag.
  bool isTag() const { return Type == wasm::WASM_SYMBOL_TYPE_TAG; }

  /// Return the Wasm symbol type, if set.
  ///
  /// \return The Wasm symbol type, or an empty optional if unset.
  std::optional<wasm::WasmSymbolType> getType() const { return Type; }

  /// Set the Wasm symbol type.
  ///
  /// \param type - Wasm symbol type to assign.
  void setType(wasm::WasmSymbolType type) { Type = type; }

  /// Return true if this symbol is marked exported.
  ///
  /// \return True if the exported flag is set.
  bool isExported() const {
    return getFlags() & wasm::WASM_SYMBOL_EXPORTED;
  }
  /// Mark this symbol as exported.
  void setExported() const {
    modifyFlags(wasm::WASM_SYMBOL_EXPORTED, wasm::WASM_SYMBOL_EXPORTED);
  }

  /// Return true if this symbol must not be stripped.
  ///
  /// \return True if the no-strip flag is set.
  bool isNoStrip() const {
    return getFlags() & wasm::WASM_SYMBOL_NO_STRIP;
  }
  /// Mark this symbol so it is not stripped.
  void setNoStrip() const {
    modifyFlags(wasm::WASM_SYMBOL_NO_STRIP, wasm::WASM_SYMBOL_NO_STRIP);
  }

  /// Return true if this symbol is thread-local.
  ///
  /// \return True if the TLS flag is set.
  bool isTLS() const { return getFlags() & wasm::WASM_SYMBOL_TLS; }
  /// Mark this symbol as thread-local.
  void setTLS() const {
    modifyFlags(wasm::WASM_SYMBOL_TLS, wasm::WASM_SYMBOL_TLS);
  }

  /// Return true if this symbol is weak.
  ///
  /// \return True if the symbol is weak.
  bool isWeak() const { return IsWeak; }
  /// Set whether this symbol is weak.
  ///
  /// \param isWeak - True if the symbol is weak.
  void setWeak(bool isWeak) { IsWeak = isWeak; }

  /// Return true if this symbol is hidden.
  ///
  /// \return True if the symbol is hidden.
  bool isHidden() const { return IsHidden; }
  /// Set whether this symbol is hidden.
  ///
  /// \param isHidden - True if the symbol is hidden.
  void setHidden(bool isHidden) { IsHidden = isHidden; }

  /// Return true if this symbol belongs to a COMDAT group.
  ///
  /// \return True if the symbol is in a COMDAT.
  bool isComdat() const { return IsComdat; }
  /// Set whether this symbol belongs to a COMDAT group.
  ///
  /// \param isComdat - True if the symbol is in a COMDAT.
  void setComdat(bool isComdat) { IsComdat = isComdat; }

  /// Return true if this symbol should be omitted from the linking section.
  ///
  /// wasm-ld understands a finite set of symbol types.  This flag allows the
  /// compiler to avoid emitting symbol table entries that would confuse the
  /// linker, unless the user specifically requests the feature.
  /// \return True if the symbol should be omitted from the linking section.
  bool omitFromLinkingSection() const { return OmitFromLinkingSection; }
  /// Mark this symbol to be omitted from the linking section.
  void setOmitFromLinkingSection() { OmitFromLinkingSection = true; }

  /// Return true if an explicit import module name is set.
  ///
  /// \return True if an import module name has been set.
  bool hasImportModule() const { return ImportModule.has_value(); }
  /// Return the import module name, or \c "env" if none was set.
  ///
  /// \return The import module name, or \c "env" if unset.
  StringRef getImportModule() const {
    if (ImportModule)
      return *ImportModule;
    // Use a default module name of "env" for now, for compatibility with
    // existing tools.
    // TODO(sbc): Find a way to specify a default value in the object format
    // without picking a hardcoded value like this.
    return "env";
  }
  /// Set the import module name.
  ///
  /// \param Name - Import module name.
  void setImportModule(StringRef Name) { ImportModule = Name; }

  /// Return true if an explicit import name is set.
  ///
  /// \return True if an import name has been set.
  bool hasImportName() const { return ImportName.has_value(); }
  /// Return the import name, or this symbol's name if none was set.
  ///
  /// \return The import name, or the symbol name if unset.
  StringRef getImportName() const {
    if (ImportName)
      return *ImportName;
    return getName();
  }
  /// Set the import name.
  ///
  /// \param Name - Import name.
  void setImportName(StringRef Name) { ImportName = Name; }

  /// Return true if an explicit export name is set.
  ///
  /// \return True if an export name has been set.
  bool hasExportName() const { return ExportName.has_value(); }
  /// Return the export name.
  ///
  /// \return The export name.
  StringRef getExportName() const { return *ExportName; }
  /// Set the export name.
  ///
  /// \param Name - Export name.
  void setExportName(StringRef Name) { ExportName = Name; }

  /// Return true if this is a table of function references.
  ///
  /// \return True if this is a funcref table.
  bool isFunctionTable() const {
    return isTable() && hasTableType() &&
           getTableType().ElemType == wasm::ValType::FUNCREF;
  }
  /// Mark this symbol as a function-reference table.
  ///
  /// \param is64 - True to set the 64-bit address-space limits flag.
  void setFunctionTable(bool is64) {
    setType(wasm::WASM_SYMBOL_TYPE_TABLE);
    uint8_t flags =
        is64 ? wasm::WASM_LIMITS_FLAG_IS_64 : wasm::WASM_LIMITS_FLAG_NONE;
    setTableType(wasm::ValType::FUNCREF, flags);
  }

  /// Mark this symbol as referenced from the GOT.
  void setUsedInGOT() const { IsUsedInGOT = true; }
  /// Return true if this symbol is referenced from the GOT.
  ///
  /// \return True if the symbol is used in the GOT.
  bool isUsedInGOT() const { return IsUsedInGOT; }

  /// Mark this symbol as used in an init array.
  void setUsedInInitArray() const { IsUsedInInitArray = true; }
  /// Return true if this symbol is used in an init array.
  ///
  /// \return True if the symbol is used in an init array.
  bool isUsedInInitArray() const { return IsUsedInInitArray; }

  /// Return the Wasm function signature, if any.
  ///
  /// \return The function signature, or null if none.
  const wasm::WasmSignature *getSignature() const { return Signature; }
  /// Set the Wasm function signature.
  ///
  /// \param Sig - Signature for this function symbol, or null.
  void setSignature(wasm::WasmSignature *Sig) { Signature = Sig; }

  /// Return the Wasm global type.
  ///
  /// \return The Wasm global type.
  const wasm::WasmGlobalType &getGlobalType() const {
    assert(GlobalType);
    return *GlobalType;
  }
  /// Set the Wasm global type.
  ///
  /// \param GT - Global type to assign.
  void setGlobalType(wasm::WasmGlobalType GT) { GlobalType = GT; }

  /// Return true if a Wasm table type is set.
  ///
  /// \return True if a table type has been set.
  bool hasTableType() const { return TableType.has_value(); }
  /// Return the Wasm table type.
  ///
  /// \return The Wasm table type.
  const wasm::WasmTableType &getTableType() const {
    assert(hasTableType());
    return *TableType;
  }
  /// Set the Wasm table type.
  ///
  /// \param TT - Table type to assign.
  void setTableType(wasm::WasmTableType TT) { TableType = TT; }
  /// Set the table type from an element type and limits flags.
  ///
  /// Declare a table with element type VT and no limits (min size 0, no max
  /// size).
  /// \param VT - Element value type for the table.
  /// \param flags - Wasm limits flags (default none).
  void setTableType(wasm::ValType VT,
                    uint8_t flags = wasm::WASM_LIMITS_FLAG_NONE) {
    wasm::WasmLimits Limits = {flags, 0, 0, 0};
    setTableType({VT, Limits});
  }
};

} // end namespace llvm

#endif // LLVM_MC_MCSYMBOLWASM_H
