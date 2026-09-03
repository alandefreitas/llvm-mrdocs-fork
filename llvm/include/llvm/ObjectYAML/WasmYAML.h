//===- WasmYAML.h - Wasm YAMLIO implementation ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation
/// of wasm binaries.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_WASMYAML_H
#define LLVM_OBJECTYAML_WASMYAML_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/Casting.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {
/// YAML representations of Wasm object files.
namespace WasmYAML {

/// Strong typedef for a Wasm section type code.
struct SectionType {
  /// Construct a zero-initialized SectionType.
  SectionType() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  SectionType(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c SectionType.
  /// \param v Value to copy.
  SectionType(const SectionType &v) = default;
  /// Copy-assign from another \c SectionType.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  SectionType &operator=(const SectionType &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  SectionType &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const SectionType &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const SectionType &rhs) const { return value < rhs.value; }
  /// Stored section type code.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a Wasm value type code.
struct ValueType {
  /// Construct a zero-initialized ValueType.
  ValueType() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ValueType(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c ValueType.
  /// \param v Value to copy.
  ValueType(const ValueType &v) = default;
  /// Copy-assign from another \c ValueType.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ValueType &operator=(const ValueType &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ValueType &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ValueType &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ValueType &rhs) const { return value < rhs.value; }
  /// Stored value type code.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a Wasm table element type code.
struct TableType {
  /// Construct a zero-initialized TableType.
  TableType() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  TableType(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c TableType.
  /// \param v Value to copy.
  TableType(const TableType &v) = default;
  /// Copy-assign from another \c TableType.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  TableType &operator=(const TableType &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  TableType &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const TableType &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const TableType &rhs) const { return value < rhs.value; }
  /// Stored table element type code.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a Wasm type signature form code.
struct SignatureForm {
  /// Construct a zero-initialized SignatureForm.
  SignatureForm() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  SignatureForm(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c SignatureForm.
  /// \param v Value to copy.
  SignatureForm(const SignatureForm &v) = default;
  /// Copy-assign from another \c SignatureForm.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  SignatureForm &operator=(const SignatureForm &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  SignatureForm &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const SignatureForm &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const SignatureForm &rhs) const { return value < rhs.value; }
  /// Stored signature form code.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a Wasm export kind code.
struct ExportKind {
  /// Construct a zero-initialized ExportKind.
  ExportKind() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ExportKind(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c ExportKind.
  /// \param v Value to copy.
  ExportKind(const ExportKind &v) = default;
  /// Copy-assign from another \c ExportKind.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ExportKind &operator=(const ExportKind &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ExportKind &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ExportKind &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ExportKind &rhs) const { return value < rhs.value; }
  /// Stored export kind code.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a Wasm opcode value.
struct Opcode {
  /// Construct a zero-initialized Opcode.
  Opcode() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  Opcode(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c Opcode.
  /// \param v Value to copy.
  Opcode(const Opcode &v) = default;
  /// Copy-assign from another \c Opcode.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  Opcode &operator=(const Opcode &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  Opcode &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const Opcode &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const Opcode &rhs) const { return value < rhs.value; }
  /// Stored opcode value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a Wasm relocation type code.
struct RelocType {
  /// Construct a zero-initialized RelocType.
  RelocType() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  RelocType(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c RelocType.
  /// \param v Value to copy.
  RelocType(const RelocType &v) = default;
  /// Copy-assign from another \c RelocType.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  RelocType &operator=(const RelocType &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  RelocType &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const RelocType &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const RelocType &rhs) const { return value < rhs.value; }
  /// Stored relocation type code.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a Wasm linking symbol flags bitmask.
struct SymbolFlags {
  /// Construct a zero-initialized SymbolFlags.
  SymbolFlags() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  SymbolFlags(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c SymbolFlags.
  /// \param v Value to copy.
  SymbolFlags(const SymbolFlags &v) = default;
  /// Copy-assign from another \c SymbolFlags.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  SymbolFlags &operator=(const SymbolFlags &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  SymbolFlags &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const SymbolFlags &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const SymbolFlags &rhs) const { return value < rhs.value; }
  /// Stored symbol flags bitmask.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a Wasm linking symbol kind code.
struct SymbolKind {
  /// Construct a zero-initialized SymbolKind.
  SymbolKind() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  SymbolKind(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c SymbolKind.
  /// \param v Value to copy.
  SymbolKind(const SymbolKind &v) = default;
  /// Copy-assign from another \c SymbolKind.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  SymbolKind &operator=(const SymbolKind &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  SymbolKind &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const SymbolKind &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const SymbolKind &rhs) const { return value < rhs.value; }
  /// Stored symbol kind code.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a Wasm data segment flags bitmask.
struct SegmentFlags {
  /// Construct a zero-initialized SegmentFlags.
  SegmentFlags() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  SegmentFlags(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c SegmentFlags.
  /// \param v Value to copy.
  SegmentFlags(const SegmentFlags &v) = default;
  /// Copy-assign from another \c SegmentFlags.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  SegmentFlags &operator=(const SegmentFlags &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  SegmentFlags &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const SegmentFlags &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const SegmentFlags &rhs) const { return value < rhs.value; }
  /// Stored segment flags bitmask.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a Wasm limits flags bitmask.
struct LimitFlags {
  /// Construct a zero-initialized LimitFlags.
  LimitFlags() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  LimitFlags(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c LimitFlags.
  /// \param v Value to copy.
  LimitFlags(const LimitFlags &v) = default;
  /// Copy-assign from another \c LimitFlags.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  LimitFlags &operator=(const LimitFlags &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  LimitFlags &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const LimitFlags &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const LimitFlags &rhs) const { return value < rhs.value; }
  /// Stored limits flags bitmask.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a Wasm COMDAT entry kind code.
struct ComdatKind {
  /// Construct a zero-initialized ComdatKind.
  ComdatKind() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ComdatKind(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c ComdatKind.
  /// \param v Value to copy.
  ComdatKind(const ComdatKind &v) = default;
  /// Copy-assign from another \c ComdatKind.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ComdatKind &operator=(const ComdatKind &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ComdatKind &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ComdatKind &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ComdatKind &rhs) const { return value < rhs.value; }
  /// Stored COMDAT kind code.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a Wasm target-feature policy prefix code.
struct FeaturePolicyPrefix {
  /// Construct a zero-initialized FeaturePolicyPrefix.
  FeaturePolicyPrefix() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  FeaturePolicyPrefix(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c FeaturePolicyPrefix.
  /// \param v Value to copy.
  FeaturePolicyPrefix(const FeaturePolicyPrefix &v) = default;
  /// Copy-assign from another \c FeaturePolicyPrefix.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  FeaturePolicyPrefix &operator=(const FeaturePolicyPrefix &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  FeaturePolicyPrefix &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const FeaturePolicyPrefix &rhs) const {
    return value == rhs.value;
  }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const FeaturePolicyPrefix &rhs) const {
    return value < rhs.value;
  }
  /// Stored feature policy prefix code.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// YAML representation of a Wasm binary file header.
struct FileHeader {
  /// Wasm binary format version.
  yaml::Hex32 Version;
};

/// YAML representation of Wasm memory or table limits.
struct Limits {
  /// Flags describing which limit fields are present.
  LimitFlags Flags;
  /// Minimum size in pages or elements.
  yaml::Hex32 Minimum;
  /// Maximum size in pages or elements, when present.
  yaml::Hex32 Maximum;
  /// Optional custom page size for memory limits.
  yaml::Hex32 PageSize;
};

/// YAML representation of a Wasm table.
struct Table {
  /// Element type stored in the table.
  TableType ElemType;
  /// Minimum and optional maximum table size.
  Limits TableLimits;
  /// Index of this table in the module.
  uint32_t Index;
};

/// YAML representation of a Wasm export.
struct Export {
  /// Exported symbol name.
  StringRef Name;
  /// Kind of entity being exported.
  ExportKind Kind;
  /// Index of the exported entity within its kind.
  uint32_t Index;
};

/// YAML representation of a Wasm initializer expression.
struct InitExpr {
  /// Construct an empty initializer expression.
  InitExpr() {}
  /// Whether \c Body holds an extended init expression instead of \c Inst.
  bool Extended;
  union {
    /// Classic MVP init expression instruction.
    wasm::WasmInitExprMVP Inst;
    /// Extended init expression bytecode payload.
    yaml::BinaryRef Body;
  };
};

/// YAML representation of a Wasm element segment.
struct ElemSegment {
  /// Element segment flags.
  uint32_t Flags;
  /// Table index this segment initializes.
  uint32_t TableNumber;
  /// Element kind for the segment entries.
  ValueType ElemKind;
  /// Offset expression within the table.
  InitExpr Offset;
  /// Function indices written into the table.
  std::vector<uint32_t> Functions;
};

/// YAML representation of a Wasm global.
struct Global {
  /// Index of this global in the module.
  uint32_t Index;
  /// Value type of the global.
  ValueType Type;
  /// Whether the global is mutable.
  bool Mutable;
  /// Initializer expression for the global.
  InitExpr Init;
};

/// YAML representation of a Wasm import.
struct Import {
  /// Construct an empty import.
  Import() {}
  /// Module name providing the import.
  StringRef Module;
  /// Field name of the imported entity.
  StringRef Field;
  /// Kind of entity being imported.
  ExportKind Kind;
  union {
    /// Type index for a function import.
    uint32_t SigIndex;
    /// Table descriptor for a table import.
    Table TableImport;
    /// Memory limits for a memory import.
    Limits Memory;
    /// Type index for a tag import.
    uint32_t TagIndex;
    /// Global descriptor for a global import.
    Global GlobalImport;
  };
};

/// YAML representation of a local-variable declaration group in a function.
struct LocalDecl {
  /// Value type of the locals in this group.
  ValueType Type;
  /// Number of locals of type \c Type.
  uint32_t Count;
};

/// YAML representation of a Wasm function body.
struct Function {
  /// Index of this function in the module.
  uint32_t Index;
  /// Local variable declaration groups.
  std::vector<LocalDecl> Locals;
  /// Function bytecode body.
  yaml::BinaryRef Body;
};

/// YAML representation of a Wasm relocation.
struct Relocation {
  /// Relocation type.
  RelocType Type;
  /// Symbol or type index referenced by the relocation.
  uint32_t Index;
  // TODO(wvo): this would strictly be better as Hex64, but that will change
  // all existing obj2yaml output.
  /// Byte offset of the relocated field within its section.
  yaml::Hex32 Offset;
  /// Optional addend applied to the relocated value.
  int64_t Addend;
};

/// YAML representation of a Wasm data segment.
struct DataSegment {
  /// Offset of this segment within the data section payload.
  uint32_t SectionOffset;
  /// Data segment initialization flags.
  uint32_t InitFlags;
  /// Memory index this segment initializes.
  uint32_t MemoryIndex;
  /// Offset expression within the memory.
  InitExpr Offset;
  /// Raw bytes written into memory.
  yaml::BinaryRef Content;
};

/// YAML representation of a name subsection entry.
struct NameEntry {
  /// Index of the named entity.
  uint32_t Index;
  /// Debug name associated with \c Index.
  StringRef Name;
};

/// YAML representation of a producers-section entry.
struct ProducerEntry {
  /// Producer tool, language, or SDK name.
  std::string Name;
  /// Producer version string.
  std::string Version;
};

/// YAML representation of a target-features-section entry.
struct FeatureEntry {
  /// Policy prefix for the feature (used, required, or disallowed).
  FeaturePolicyPrefix Prefix;
  /// Feature name string.
  std::string Name;
};

/// YAML representation of linking-metadata segment info.
struct SegmentInfo {
  /// Index of the data segment.
  uint32_t Index;
  /// Linker-visible segment name.
  StringRef Name;
  /// Alignment of the segment as a power of two.
  uint32_t Alignment;
  /// Segment flags.
  SegmentFlags Flags;
};

/// YAML representation of a Wasm function type signature.
struct Signature {
  /// Index of this signature in the type section.
  uint32_t Index;
  /// Signature form discriminator (typically a function type).
  SignatureForm Form = wasm::WASM_TYPE_FUNC;
  /// Parameter value types.
  std::vector<ValueType> ParamTypes;
  /// Result value types.
  std::vector<ValueType> ReturnTypes;
};

/// YAML representation of a linking-section symbol table entry.
struct SymbolInfo {
  /// Index of this symbol in the symbol table.
  uint32_t Index;
  /// Symbol name.
  StringRef Name;
  /// Kind of symbol.
  SymbolKind Kind;
  /// Symbol flags.
  SymbolFlags Flags;
  union {
    /// Element index for function, global, table, or section symbols.
    uint32_t ElementIndex;
    /// Data reference for data symbols.
    wasm::WasmDataReference DataRef;
    /// Common-symbol reference for common symbols.
    wasm::WasmCommonReference CommonRef;
  };
};

/// YAML representation of a linking-section init function.
struct InitFunction {
  /// Initialization priority (lower runs first).
  uint32_t Priority;
  /// Symbol table index of the init function.
  uint32_t Symbol;
};

/// YAML representation of a COMDAT group entry.
struct ComdatEntry {
  /// Kind of entity in the COMDAT group.
  ComdatKind Kind;
  /// Index of the entity within its kind.
  uint32_t Index;
};

/// YAML representation of a COMDAT group.
struct Comdat {
  /// COMDAT group name.
  StringRef Name;
  /// Entries that belong to this COMDAT group.
  std::vector<ComdatEntry> Entries;
};

/// Base YAML representation of a Wasm section.
struct LLVM_ABI Section {
  /// Construct a section with the given type.
  /// \param SecType Wasm section type code.
  explicit Section(SectionType SecType) : Type(SecType) {}
  /// Destroy a section.
  virtual ~Section();

  /// Wasm section type code.
  SectionType Type;
  /// Relocations applying to this section.
  std::vector<Relocation> Relocations;
  /// Optional LEB encoding length override for the section-size field.
  std::optional<uint8_t> HeaderSecSizeEncodingLen;
};

/// YAML representation of a Wasm custom section.
struct CustomSection : Section {
  /// Construct a custom section with the given name.
  /// \param Name Custom section name.
  explicit CustomSection(StringRef Name)
      : Section(wasm::WASM_SEC_CUSTOM), Name(Name) {}

  /// Return true if \p S is a custom section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_CUSTOM;
  }

  /// Custom section name.
  StringRef Name;
  /// Raw custom section payload.
  yaml::BinaryRef Payload;
};

/// YAML representation of a dylink import symbol info entry.
struct DylinkImportInfo {
  /// Module name of the imported symbol.
  StringRef Module;
  /// Field name of the imported symbol.
  StringRef Field;
  /// Symbol flags for the import.
  SymbolFlags Flags;
};

/// YAML representation of a dylink export symbol info entry.
struct DylinkExportInfo {
  /// Exported symbol name.
  StringRef Name;
  /// Symbol flags for the export.
  SymbolFlags Flags;
};

/// YAML representation of the \c dylink.0 custom section.
struct DylinkSection : CustomSection {
  /// Construct an empty dylink.0 section.
  DylinkSection() : CustomSection("dylink.0") {}

  /// Return true if \p S is a dylink.0 section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    auto C = dyn_cast<CustomSection>(S);
    return C && C->Name == "dylink.0";
  }

  /// Memory size required by the dynamic library.
  uint32_t MemorySize;
  /// Memory alignment required by the dynamic library.
  uint32_t MemoryAlignment;
  /// Table size required by the dynamic library.
  uint32_t TableSize;
  /// Table alignment required by the dynamic library.
  uint32_t TableAlignment;
  /// Shared libraries needed by this module.
  std::vector<StringRef> Needed;
  /// Per-import symbol info entries.
  std::vector<DylinkImportInfo> ImportInfo;
  /// Per-export symbol info entries.
  std::vector<DylinkExportInfo> ExportInfo;
  /// Runtime search paths for dependent libraries.
  std::vector<StringRef> RuntimePath;
};

/// YAML representation of the \c name custom section.
struct NameSection : CustomSection {
  /// Construct an empty name section.
  NameSection() : CustomSection("name") {}

  /// Return true if \p S is a name section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    auto C = dyn_cast<CustomSection>(S);
    return C && C->Name == "name";
  }

  /// Function debug names.
  std::vector<NameEntry> FunctionNames;
  /// Global debug names.
  std::vector<NameEntry> GlobalNames;
  /// Data segment debug names.
  std::vector<NameEntry> DataSegmentNames;
};

/// YAML representation of the \c linking custom section.
struct LinkingSection : CustomSection {
  /// Construct an empty linking section.
  LinkingSection() : CustomSection("linking") {}

  /// Return true if \p S is a linking section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    auto C = dyn_cast<CustomSection>(S);
    return C && C->Name == "linking";
  }

  /// Linking metadata format version.
  uint32_t Version;
  /// Linker symbol table.
  std::vector<SymbolInfo> SymbolTable;
  /// Data segment metadata.
  std::vector<SegmentInfo> SegmentInfos;
  /// Static constructors / init functions.
  std::vector<InitFunction> InitFunctions;
  /// COMDAT groups.
  std::vector<Comdat> Comdats;
};

/// YAML representation of the \c producers custom section.
struct ProducersSection : CustomSection {
  /// Construct an empty producers section.
  ProducersSection() : CustomSection("producers") {}

  /// Return true if \p S is a producers section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    auto C = dyn_cast<CustomSection>(S);
    return C && C->Name == "producers";
  }

  /// Languages used to produce the module.
  std::vector<ProducerEntry> Languages;
  /// Tools used to produce the module.
  std::vector<ProducerEntry> Tools;
  /// SDKs used to produce the module.
  std::vector<ProducerEntry> SDKs;
};

/// YAML representation of the \c target_features custom section.
struct TargetFeaturesSection : CustomSection {
  /// Construct an empty target_features section.
  TargetFeaturesSection() : CustomSection("target_features") {}

  /// Return true if \p S is a target_features section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    auto C = dyn_cast<CustomSection>(S);
    return C && C->Name == "target_features";
  }

  /// Declared target features and their policies.
  std::vector<FeatureEntry> Features;
};

/// YAML representation of the Wasm type section.
struct TypeSection : Section {
  /// Construct an empty type section.
  TypeSection() : Section(wasm::WASM_SEC_TYPE) {}

  /// Return true if \p S is a type section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_TYPE;
  }

  /// Function type signatures defined by this section.
  std::vector<Signature> Signatures;
};

/// YAML representation of the Wasm import section.
struct ImportSection : Section {
  /// Construct an empty import section.
  ImportSection() : Section(wasm::WASM_SEC_IMPORT) {}

  /// Return true if \p S is an import section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_IMPORT;
  }

  /// Imported entities.
  std::vector<Import> Imports;
};

/// YAML representation of the Wasm function section.
struct FunctionSection : Section {
  /// Construct an empty function section.
  FunctionSection() : Section(wasm::WASM_SEC_FUNCTION) {}

  /// Return true if \p S is a function section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_FUNCTION;
  }

  /// Type indices for each function declaration.
  std::vector<uint32_t> FunctionTypes;
};

/// YAML representation of the Wasm table section.
struct TableSection : Section {
  /// Construct an empty table section.
  TableSection() : Section(wasm::WASM_SEC_TABLE) {}

  /// Return true if \p S is a table section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_TABLE;
  }

  /// Tables defined by this section.
  std::vector<Table> Tables;
};

/// YAML representation of the Wasm memory section.
struct MemorySection : Section {
  /// Construct an empty memory section.
  MemorySection() : Section(wasm::WASM_SEC_MEMORY) {}

  /// Return true if \p S is a memory section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_MEMORY;
  }

  /// Memories defined by this section.
  std::vector<Limits> Memories;
};

/// YAML representation of the Wasm tag section.
struct TagSection : Section {
  /// Construct an empty tag section.
  TagSection() : Section(wasm::WASM_SEC_TAG) {}

  /// Return true if \p S is a tag section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_TAG;
  }

  /// Type indices for each tag declaration.
  std::vector<uint32_t> TagTypes;
};

/// YAML representation of the Wasm global section.
struct GlobalSection : Section {
  /// Construct an empty global section.
  GlobalSection() : Section(wasm::WASM_SEC_GLOBAL) {}

  /// Return true if \p S is a global section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_GLOBAL;
  }

  /// Globals defined by this section.
  std::vector<Global> Globals;
};

/// YAML representation of the Wasm export section.
struct ExportSection : Section {
  /// Construct an empty export section.
  ExportSection() : Section(wasm::WASM_SEC_EXPORT) {}

  /// Return true if \p S is an export section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_EXPORT;
  }

  /// Exported entities.
  std::vector<Export> Exports;
};

/// YAML representation of the Wasm start section.
struct StartSection : Section {
  /// Construct an empty start section.
  StartSection() : Section(wasm::WASM_SEC_START) {}

  /// Return true if \p S is a start section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_START;
  }

  /// Function index of the module start function.
  uint32_t StartFunction;
};

/// YAML representation of the Wasm element section.
struct ElemSection : Section {
  /// Construct an empty element section.
  ElemSection() : Section(wasm::WASM_SEC_ELEM) {}

  /// Return true if \p S is an element section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_ELEM;
  }

  /// Element segments defined by this section.
  std::vector<ElemSegment> Segments;
};

/// YAML representation of the Wasm code section.
struct CodeSection : Section {
  /// Construct an empty code section.
  CodeSection() : Section(wasm::WASM_SEC_CODE) {}

  /// Return true if \p S is a code section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_CODE;
  }

  /// Function bodies defined by this section.
  std::vector<Function> Functions;
};

/// YAML representation of the Wasm data section.
struct DataSection : Section {
  /// Construct an empty data section.
  DataSection() : Section(wasm::WASM_SEC_DATA) {}

  /// Return true if \p S is a data section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_DATA;
  }

  /// Data segments defined by this section.
  std::vector<DataSegment> Segments;
};

/// YAML representation of the Wasm data count section.
struct DataCountSection : Section {
  /// Construct an empty data count section.
  DataCountSection() : Section(wasm::WASM_SEC_DATACOUNT) {}

  /// Return true if \p S is a data count section.
  /// \param S Section to test.
  /// \return True if \p S has this section kind.
  static bool classof(const Section *S) {
    return S->Type == wasm::WASM_SEC_DATACOUNT;
  }

  /// Number of data segments declared by the module.
  uint32_t Count;
};

/// YAML representation of a complete Wasm object file.
struct Object {
  /// Wasm file header.
  FileHeader Header;
  /// Sections contained in the object, in file order.
  std::vector<std::unique_ptr<Section>> Sections;
};

} // end namespace WasmYAML
} // end namespace llvm

namespace llvm {
namespace yaml {

/// Sequences of owned Wasm sections use block formatting.
template <>
struct SequenceElementTraits<std::unique_ptr<llvm::WasmYAML::Section>> {
  /// Emit sequences of owned Wasm sections in block style.
  static const bool flow = false;
};

/// Sequences of Wasm signatures use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::Signature> {
  /// Emit sequences of Wasm signatures in block style.
  static const bool flow = false;
};

/// Sequences of Wasm value types use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::ValueType> {
  /// Emit sequences of Wasm value types in block style.
  static const bool flow = false;
};

/// Sequences of Wasm tables use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::Table> {
  /// Emit sequences of Wasm tables in block style.
  static const bool flow = false;
};

/// Sequences of Wasm imports use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::Import> {
  /// Emit sequences of Wasm imports in block style.
  static const bool flow = false;
};

/// Sequences of Wasm exports use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::Export> {
  /// Emit sequences of Wasm exports in block style.
  static const bool flow = false;
};

/// Sequences of Wasm element segments use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::ElemSegment> {
  /// Emit sequences of Wasm element segments in block style.
  static const bool flow = false;
};

/// Sequences of Wasm limits use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::Limits> {
  /// Emit sequences of Wasm limits in block style.
  static const bool flow = false;
};

/// Sequences of Wasm data segments use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::DataSegment> {
  /// Emit sequences of Wasm data segments in block style.
  static const bool flow = false;
};

/// Sequences of Wasm globals use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::Global> {
  /// Emit sequences of Wasm globals in block style.
  static const bool flow = false;
};

/// Sequences of Wasm functions use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::Function> {
  /// Emit sequences of Wasm functions in block style.
  static const bool flow = false;
};

/// Sequences of Wasm local declarations use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::LocalDecl> {
  /// Emit sequences of Wasm local declarations in block style.
  static const bool flow = false;
};

/// Sequences of Wasm relocations use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::Relocation> {
  /// Emit sequences of Wasm relocations in block style.
  static const bool flow = false;
};

/// Sequences of Wasm name entries use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::NameEntry> {
  /// Emit sequences of Wasm name entries in block style.
  static const bool flow = false;
};

/// Sequences of Wasm producer entries use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::ProducerEntry> {
  /// Emit sequences of Wasm producer entries in block style.
  static const bool flow = false;
};

/// Sequences of Wasm feature entries use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::FeatureEntry> {
  /// Emit sequences of Wasm feature entries in block style.
  static const bool flow = false;
};

/// Sequences of Wasm segment infos use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::SegmentInfo> {
  /// Emit sequences of Wasm segment infos in block style.
  static const bool flow = false;
};

/// Sequences of Wasm symbol infos use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::SymbolInfo> {
  /// Emit sequences of Wasm symbol infos in block style.
  static const bool flow = false;
};

/// Sequences of Wasm init functions use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::InitFunction> {
  /// Emit sequences of Wasm init functions in block style.
  static const bool flow = false;
};

/// Sequences of Wasm COMDAT entries use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::ComdatEntry> {
  /// Emit sequences of Wasm COMDAT entries in block style.
  static const bool flow = false;
};

/// Sequences of Wasm COMDATs use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::Comdat> {
  /// Emit sequences of Wasm COMDATs in block style.
  static const bool flow = false;
};

/// Sequences of Wasm dylink import infos use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::DylinkImportInfo> {
  /// Emit sequences of Wasm dylink import infos in block style.
  static const bool flow = false;
};

/// Sequences of Wasm dylink export infos use block formatting.
template <> struct SequenceElementTraits<llvm::WasmYAML::DylinkExportInfo> {
  /// Emit sequences of Wasm dylink export infos in block style.
  static const bool flow = false;
};

/// YAMLIO mapping traits for \c WasmYAML::FileHeader.
template <> struct MappingTraits<WasmYAML::FileHeader> {
  /// Map Wasm file header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param FileHdr File header being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::FileHeader &FileHdr);
};

/// YAMLIO mapping traits for owned Wasm sections.
template <> struct MappingTraits<std::unique_ptr<WasmYAML::Section>> {
  /// Map an owned Wasm section to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Section Section being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               std::unique_ptr<WasmYAML::Section> &Section);
};

/// YAMLIO mapping traits for \c WasmYAML::Object.
template <> struct MappingTraits<WasmYAML::Object> {
  /// Map Wasm object fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Object Object being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::Object &Object);
};

/// YAMLIO mapping traits for \c WasmYAML::Import.
template <> struct MappingTraits<WasmYAML::Import> {
  /// Map Wasm import fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Import Import being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::Import &Import);
};

/// YAMLIO mapping traits for \c WasmYAML::Export.
template <> struct MappingTraits<WasmYAML::Export> {
  /// Map Wasm export fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Export Export being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::Export &Export);
};

/// YAMLIO mapping traits for \c WasmYAML::Global.
template <> struct MappingTraits<WasmYAML::Global> {
  /// Map Wasm global fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Global Global being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::Global &Global);
};

/// YAMLIO scalar bitset traits for \c WasmYAML::LimitFlags.
template <> struct ScalarBitSetTraits<WasmYAML::LimitFlags> {
  /// Map Wasm limit flags to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Limit flags being mapped.
  LLVM_ABI static void bitset(IO &IO, WasmYAML::LimitFlags &Value);
};

/// YAMLIO scalar bitset traits for \c WasmYAML::SymbolFlags.
template <> struct ScalarBitSetTraits<WasmYAML::SymbolFlags> {
  /// Map Wasm symbol flags to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Symbol flags being mapped.
  LLVM_ABI static void bitset(IO &IO, WasmYAML::SymbolFlags &Value);
};

/// YAMLIO scalar enumeration traits for \c WasmYAML::SymbolKind.
template <> struct ScalarEnumerationTraits<WasmYAML::SymbolKind> {
  /// Map Wasm symbol kind enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Kind Symbol kind being mapped.
  LLVM_ABI static void enumeration(IO &IO, WasmYAML::SymbolKind &Kind);
};

/// YAMLIO scalar bitset traits for \c WasmYAML::SegmentFlags.
template <> struct ScalarBitSetTraits<WasmYAML::SegmentFlags> {
  /// Map Wasm segment flags to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Segment flags being mapped.
  LLVM_ABI static void bitset(IO &IO, WasmYAML::SegmentFlags &Value);
};

/// YAMLIO scalar enumeration traits for \c WasmYAML::SectionType.
template <> struct ScalarEnumerationTraits<WasmYAML::SectionType> {
  /// Map Wasm section type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Type Section type being mapped.
  LLVM_ABI static void enumeration(IO &IO, WasmYAML::SectionType &Type);
};

/// YAMLIO mapping traits for \c WasmYAML::Signature.
template <> struct MappingTraits<WasmYAML::Signature> {
  /// Map Wasm signature fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Signature Signature being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::Signature &Signature);
};

/// YAMLIO mapping traits for \c WasmYAML::Table.
template <> struct MappingTraits<WasmYAML::Table> {
  /// Map Wasm table fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Table Table being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::Table &Table);
};

/// YAMLIO mapping traits for \c WasmYAML::Limits.
template <> struct MappingTraits<WasmYAML::Limits> {
  /// Map Wasm limits fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Limits Limits being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::Limits &Limits);
};

/// YAMLIO mapping traits for \c WasmYAML::Function.
template <> struct MappingTraits<WasmYAML::Function> {
  /// Map Wasm function fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Function Function being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::Function &Function);
};

/// YAMLIO mapping traits for \c WasmYAML::Relocation.
template <> struct MappingTraits<WasmYAML::Relocation> {
  /// Map Wasm relocation fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Relocation Relocation being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::Relocation &Relocation);
};

/// YAMLIO mapping traits for \c WasmYAML::NameEntry.
template <> struct MappingTraits<WasmYAML::NameEntry> {
  /// Map Wasm name entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param NameEntry Name entry being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::NameEntry &NameEntry);
};

/// YAMLIO mapping traits for \c WasmYAML::ProducerEntry.
template <> struct MappingTraits<WasmYAML::ProducerEntry> {
  /// Map Wasm producer entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ProducerEntry Producer entry being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::ProducerEntry &ProducerEntry);
};

/// YAMLIO scalar enumeration traits for \c WasmYAML::FeaturePolicyPrefix.
template <> struct ScalarEnumerationTraits<WasmYAML::FeaturePolicyPrefix> {
  /// Map Wasm feature-policy prefix enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Prefix Feature policy prefix being mapped.
  LLVM_ABI static void enumeration(IO &IO,
                                   WasmYAML::FeaturePolicyPrefix &Prefix);
};

/// YAMLIO mapping traits for \c WasmYAML::FeatureEntry.
template <> struct MappingTraits<WasmYAML::FeatureEntry> {
  /// Map Wasm feature entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param FeatureEntry Feature entry being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::FeatureEntry &FeatureEntry);
};

/// YAMLIO mapping traits for \c WasmYAML::SegmentInfo.
template <> struct MappingTraits<WasmYAML::SegmentInfo> {
  /// Map Wasm segment info fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param SegmentInfo Segment info being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::SegmentInfo &SegmentInfo);
};

/// YAMLIO mapping traits for \c WasmYAML::LocalDecl.
template <> struct MappingTraits<WasmYAML::LocalDecl> {
  /// Map Wasm local declaration fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param LocalDecl Local declaration being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::LocalDecl &LocalDecl);
};

/// YAMLIO mapping traits for \c WasmYAML::InitExpr.
template <> struct MappingTraits<WasmYAML::InitExpr> {
  /// Map Wasm init expression fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Expr Init expression being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::InitExpr &Expr);
};

/// YAMLIO mapping traits for \c WasmYAML::DataSegment.
template <> struct MappingTraits<WasmYAML::DataSegment> {
  /// Map Wasm data segment fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Segment Data segment being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::DataSegment &Segment);
};

/// YAMLIO mapping traits for \c WasmYAML::ElemSegment.
template <> struct MappingTraits<WasmYAML::ElemSegment> {
  /// Map Wasm element segment fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Segment Element segment being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::ElemSegment &Segment);
};

/// YAMLIO mapping traits for \c WasmYAML::SymbolInfo.
template <> struct MappingTraits<WasmYAML::SymbolInfo> {
  /// Map Wasm symbol info fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Info Symbol info being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::SymbolInfo &Info);
};

/// YAMLIO mapping traits for \c WasmYAML::InitFunction.
template <> struct MappingTraits<WasmYAML::InitFunction> {
  /// Map Wasm init function fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Init Init function being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::InitFunction &Init);
};

/// YAMLIO scalar enumeration traits for \c WasmYAML::ComdatKind.
template <> struct ScalarEnumerationTraits<WasmYAML::ComdatKind> {
  /// Map Wasm COMDAT kind enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Kind COMDAT kind being mapped.
  LLVM_ABI static void enumeration(IO &IO, WasmYAML::ComdatKind &Kind);
};

/// YAMLIO mapping traits for \c WasmYAML::ComdatEntry.
template <> struct MappingTraits<WasmYAML::ComdatEntry> {
  /// Map Wasm COMDAT entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ComdatEntry COMDAT entry being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::ComdatEntry &ComdatEntry);
};

/// YAMLIO mapping traits for \c WasmYAML::Comdat.
template <> struct MappingTraits<WasmYAML::Comdat> {
  /// Map Wasm COMDAT fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Comdat COMDAT being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::Comdat &Comdat);
};

/// YAMLIO scalar enumeration traits for \c WasmYAML::ValueType.
template <> struct ScalarEnumerationTraits<WasmYAML::ValueType> {
  /// Map Wasm value type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Type Value type being mapped.
  LLVM_ABI static void enumeration(IO &IO, WasmYAML::ValueType &Type);
};

/// YAMLIO scalar enumeration traits for \c WasmYAML::ExportKind.
template <> struct ScalarEnumerationTraits<WasmYAML::ExportKind> {
  /// Map Wasm export kind enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Kind Export kind being mapped.
  LLVM_ABI static void enumeration(IO &IO, WasmYAML::ExportKind &Kind);
};

/// YAMLIO scalar enumeration traits for \c WasmYAML::TableType.
template <> struct ScalarEnumerationTraits<WasmYAML::TableType> {
  /// Map Wasm table type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Type Table type being mapped.
  LLVM_ABI static void enumeration(IO &IO, WasmYAML::TableType &Type);
};

/// YAMLIO scalar enumeration traits for \c WasmYAML::Opcode.
template <> struct ScalarEnumerationTraits<WasmYAML::Opcode> {
  /// Map Wasm opcode enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Opcode Opcode being mapped.
  LLVM_ABI static void enumeration(IO &IO, WasmYAML::Opcode &Opcode);
};

/// YAMLIO scalar enumeration traits for \c WasmYAML::RelocType.
template <> struct ScalarEnumerationTraits<WasmYAML::RelocType> {
  /// Map Wasm relocation type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Kind Relocation type being mapped.
  LLVM_ABI static void enumeration(IO &IO, WasmYAML::RelocType &Kind);
};

/// YAMLIO mapping traits for \c WasmYAML::DylinkImportInfo.
template <> struct MappingTraits<WasmYAML::DylinkImportInfo> {
  /// Map Wasm dylink import info fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Info Dylink import info being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::DylinkImportInfo &Info);
};

/// YAMLIO mapping traits for \c WasmYAML::DylinkExportInfo.
template <> struct MappingTraits<WasmYAML::DylinkExportInfo> {
  /// Map Wasm dylink export info fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Info Dylink export info being mapped.
  LLVM_ABI static void mapping(IO &IO, WasmYAML::DylinkExportInfo &Info);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_WASMYAML_H
