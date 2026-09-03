//===- Wasm.h - Wasm object file format -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines manifest constants for the wasm object file format.
// See: https://github.com/WebAssembly/design/blob/main/BinaryEncoding.md
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_WASM_H
#define LLVM_BINARYFORMAT_WASM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {
/// Constants and structures for the WebAssembly binary object-file format.
namespace wasm {

/// Object-file magic string ("\\0asm").
const char WasmMagic[] = {'\0', 'a', 's', 'm'};
/// Wasm binary format version number.
const uint32_t WasmVersion = 0x1;
/// Wasm linking metadata version number.
const uint32_t WasmMetadataVersion = 0x2;
/// Default Wasm linear-memory page size in bytes (65536).
///
/// Wasm uses a 64k page size by default (but the custom-page-sizes proposal
/// allows changing it).
const uint32_t WasmDefaultPageSize = 65536;

/// Section id codes used in the Wasm binary format.
enum : unsigned {
  WASM_SEC_CUSTOM = 0,     ///< Custom / User-defined section
  WASM_SEC_TYPE = 1,       ///< Function signature declarations
  WASM_SEC_IMPORT = 2,     ///< Import declarations
  WASM_SEC_FUNCTION = 3,   ///< Function declarations
  WASM_SEC_TABLE = 4,      ///< Indirect function table and other tables
  WASM_SEC_MEMORY = 5,     ///< Memory attributes
  WASM_SEC_GLOBAL = 6,     ///< Global declarations
  WASM_SEC_EXPORT = 7,     ///< Exports
  WASM_SEC_START = 8,      ///< Start function declaration
  WASM_SEC_ELEM = 9,       ///< Elements section
  WASM_SEC_CODE = 10,      ///< Function bodies (code)
  WASM_SEC_DATA = 11,      ///< Data segments
  WASM_SEC_DATACOUNT = 12, ///< Data segment count
  WASM_SEC_TAG = 13,       ///< Tag declarations
  WASM_SEC_LAST_KNOWN = WASM_SEC_TAG, ///< Highest known section id
};

/// Type immediate encodings used in various contexts.
enum : unsigned {
  WASM_TYPE_I32 = 0x7F,          ///< 32-bit integer type
  WASM_TYPE_I64 = 0x7E,          ///< 64-bit integer type
  WASM_TYPE_F32 = 0x7D,          ///< 32-bit floating-point type
  WASM_TYPE_F64 = 0x7C,          ///< 64-bit floating-point type
  WASM_TYPE_V128 = 0x7B,         ///< 128-bit SIMD vector type
  WASM_TYPE_NULLFUNCREF = 0x73,  ///< Nullable funcref heap type
  WASM_TYPE_NULLEXTERNREF = 0x72, ///< Nullable externref heap type
  WASM_TYPE_NULLEXNREF = 0x74,   ///< Nullable exnref heap type
  WASM_TYPE_NULLREF = 0x71,      ///< Nullable ref (bottom) heap type
  WASM_TYPE_FUNCREF = 0x70,      ///< Function reference type
  WASM_TYPE_EXTERNREF = 0x6F,    ///< External reference type
  WASM_TYPE_EXNREF = 0x69,       ///< Exception reference type
  WASM_TYPE_ANYREF = 0x6E,       ///< Any reference type
  WASM_TYPE_EQREF = 0x6D,        ///< Eq reference type
  WASM_TYPE_I31REF = 0x6C,       ///< i31 reference type
  WASM_TYPE_STRUCTREF = 0x6B,    ///< Struct reference type
  WASM_TYPE_ARRAYREF = 0x6A,     ///< Array reference type
  WASM_TYPE_NONNULLABLE = 0x64,  ///< Non-nullable reference qualifier
  WASM_TYPE_NULLABLE = 0x63,     ///< Nullable reference qualifier
  WASM_TYPE_FUNC = 0x60,         ///< Function type form
  WASM_TYPE_ARRAY = 0x5E,        ///< Array type form
  WASM_TYPE_STRUCT = 0x5F,       ///< Struct type form
  WASM_TYPE_SUB = 0x50,          ///< Subtype declaration
  WASM_TYPE_SUB_FINAL = 0x4F,    ///< Final subtype declaration
  WASM_TYPE_REC = 0x4E,          ///< Recursive type group
  WASM_TYPE_NORESULT = 0x40, ///< Block type with no result values
};

/// Memory ordering encodings for atomic instructions.
enum : unsigned {
  WASM_MEM_ORDER_SEQ_CST = 0x00, ///< Sequentially consistent ordering
  WASM_MEM_ORDER_ACQ_REL = 0x01, ///< Acquire-release ordering
  /// RMW/CMPXCHG acquire-release with both orderings matching.
  ///
  /// RMW/CMPXCHG operations have 2 orderings but they must currently match.
  WASM_MEM_ORDER_RMW_ACQ_REL = 0x11,
};
/// Bit set in a memarg when an explicit memory ordering is present.
const unsigned WASM_MEMARG_HAS_MEM_ORDER = 0x10;

/// Kinds of externals (for imports and exports).
enum : unsigned {
  WASM_EXTERNAL_FUNCTION = 0x0, ///< Function import or export
  WASM_EXTERNAL_TABLE = 0x1,    ///< Table import or export
  WASM_EXTERNAL_MEMORY = 0x2,   ///< Memory import or export
  WASM_EXTERNAL_GLOBAL = 0x3,   ///< Global import or export
  WASM_EXTERNAL_TAG = 0x4,      ///< Tag import or export
};

/// Opcodes used in initializer expressions.
enum : unsigned {
  WASM_OPCODE_END = 0x0b,         ///< End of expression
  WASM_OPCODE_CALL = 0x10,        ///< Direct call
  WASM_OPCODE_LOCAL_GET = 0x20,   ///< Get local variable
  WASM_OPCODE_LOCAL_SET = 0x21,   ///< Set local variable
  WASM_OPCODE_LOCAL_TEE = 0x22,   ///< Tee local variable
  WASM_OPCODE_GLOBAL_GET = 0x23,  ///< Get global variable
  WASM_OPCODE_GLOBAL_SET = 0x24,  ///< Set global variable
  WASM_OPCODE_I32_STORE = 0x36,   ///< Store i32
  WASM_OPCODE_I64_STORE = 0x37,   ///< Store i64
  WASM_OPCODE_I32_CONST = 0x41,   ///< i32 constant
  WASM_OPCODE_I64_CONST = 0x42,   ///< i64 constant
  WASM_OPCODE_F32_CONST = 0x43,   ///< f32 constant
  WASM_OPCODE_F64_CONST = 0x44,   ///< f64 constant
  WASM_OPCODE_I32_ADD = 0x6a,     ///< i32 addition
  WASM_OPCODE_I32_SUB = 0x6b,     ///< i32 subtraction
  WASM_OPCODE_I32_MUL = 0x6c,     ///< i32 multiplication
  WASM_OPCODE_I64_ADD = 0x7c,     ///< i64 addition
  WASM_OPCODE_I64_SUB = 0x7d,     ///< i64 subtraction
  WASM_OPCODE_I64_MUL = 0x7e,     ///< i64 multiplication
  WASM_OPCODE_REF_NULL = 0xd0,    ///< Null reference
  WASM_OPCODE_REF_FUNC = 0xd2,    ///< Function reference
  WASM_OPCODE_GC_PREFIX = 0xfb,   ///< GC opcode prefix byte
};

/// Opcodes in the GC-prefixed space (0xfb).
enum : unsigned {
  WASM_OPCODE_STRUCT_NEW = 0x00,         ///< Allocate and initialize a struct
  WASM_OPCODE_STRUCT_NEW_DEFAULT = 0x01, ///< Allocate a default-initialized struct
  WASM_OPCODE_ARRAY_NEW = 0x06,          ///< Allocate and initialize an array
  WASM_OPCODE_ARRAY_NEW_DEFAULT = 0x07,  ///< Allocate a default-initialized array
  WASM_OPCODE_ARRAY_NEW_FIXED = 0x08,    ///< Allocate a fixed-length array
  WASM_OPCODE_REF_I31 = 0x1c,            ///< Create an i31 reference
  // any.convert_extern and extern.convert_any don't seem to be supported by
  // Binaryen.
};

/// Opcodes used in synthetic functions.
enum : unsigned {
  WASM_OPCODE_BLOCK = 0x02,            ///< Begin a block
  WASM_OPCODE_BR = 0x0c,               ///< Branch
  WASM_OPCODE_BR_TABLE = 0x0e,         ///< Indirect branch table
  WASM_OPCODE_RETURN = 0x0f,           ///< Return from function
  WASM_OPCODE_DROP = 0x1a,             ///< Drop a value
  WASM_OPCODE_MISC_PREFIX = 0xfc,      ///< Misc opcode prefix byte
  WASM_OPCODE_MEMORY_INIT = 0x08,      ///< Initialize memory from a data segment
  WASM_OPCODE_MEMORY_FILL = 0x0b,      ///< Fill memory with a byte value
  WASM_OPCODE_DATA_DROP = 0x09,        ///< Drop a data segment
  WASM_OPCODE_ATOMICS_PREFIX = 0xfe,   ///< Atomics opcode prefix byte
  WASM_OPCODE_ATOMIC_NOTIFY = 0x00,    ///< Atomic notify
  WASM_OPCODE_I32_ATOMIC_WAIT = 0x01,  ///< Atomic wait on i32
  WASM_OPCODE_I32_ATOMIC_STORE = 0x17, ///< Atomic store of i32
  WASM_OPCODE_I32_RMW_CMPXCHG = 0x48,  ///< Atomic compare-exchange of i32
};

/// Sub-opcodes for catch clauses in a try_table instruction.
enum : unsigned {
  WASM_OPCODE_CATCH = 0x00,         ///< Catch a tagged exception
  WASM_OPCODE_CATCH_REF = 0x01,     ///< Catch a tagged exception with exnref
  WASM_OPCODE_CATCH_ALL = 0x02,     ///< Catch any exception
  WASM_OPCODE_CATCH_ALL_REF = 0x03, ///< Catch any exception with exnref
};

/// Flags describing table and memory limit encodings.
enum : unsigned {
  WASM_LIMITS_FLAG_NONE = 0x0,          ///< No limit flags set
  WASM_LIMITS_FLAG_HAS_MAX = 0x1,       ///< Maximum limit is present
  WASM_LIMITS_FLAG_IS_SHARED = 0x2,     ///< Limits describe shared memory
  WASM_LIMITS_FLAG_IS_64 = 0x4,         ///< Limits use i64 addresses
  WASM_LIMITS_FLAG_HAS_PAGE_SIZE = 0x8, ///< Custom page size is present
};

/// Flags for data segment encodings.
enum : unsigned {
  WASM_DATA_SEGMENT_IS_PASSIVE = 0x01,   ///< Segment is passive
  WASM_DATA_SEGMENT_HAS_MEMINDEX = 0x02, ///< Segment encodes a memory index
};

/// Flags for element segment encodings.
enum : unsigned {
  WASM_ELEM_SEGMENT_IS_PASSIVE = 0x01,       ///< Segment is passive
  WASM_ELEM_SEGMENT_IS_DECLARATIVE = 0x02,   ///< Declarative if passive == 1
  WASM_ELEM_SEGMENT_HAS_TABLE_NUMBER = 0x02, ///< Has table number if passive == 0
  WASM_ELEM_SEGMENT_HAS_INIT_EXPRS = 0x04,   ///< Uses init expressions
};
/// Mask of element-segment flags that imply an element type descriptor.
const unsigned WASM_ELEM_SEGMENT_MASK_HAS_ELEM_DESC = 0x3;

/// Feature policy prefixes used in the custom "target_features" section.
enum : uint8_t {
  WASM_FEATURE_PREFIX_USED = '+',      ///< Feature is used
  WASM_FEATURE_PREFIX_DISALLOWED = '-', ///< Feature is disallowed
};

/// Kind codes used in the custom "name" section.
enum : unsigned {
  WASM_NAMES_MODULE = 0,       ///< Module name subsection
  WASM_NAMES_FUNCTION = 1,     ///< Function name subsection
  WASM_NAMES_LOCAL = 2,        ///< Local name subsection
  WASM_NAMES_GLOBAL = 7,       ///< Global name subsection
  WASM_NAMES_DATA_SEGMENT = 9, ///< Data-segment name subsection
};

/// Kind codes used in the custom "linking" section.
enum : unsigned {
  WASM_SEGMENT_INFO = 0x5,  ///< Segment info subsection
  WASM_INIT_FUNCS = 0x6,    ///< Init functions subsection
  WASM_COMDAT_INFO = 0x7,   ///< Comdat info subsection
  WASM_SYMBOL_TABLE = 0x8,  ///< Symbol table subsection
};

/// Kind codes used in the custom "dylink" section.
enum : unsigned {
  WASM_DYLINK_MEM_INFO = 0x1,     ///< Memory and table size info
  WASM_DYLINK_NEEDED = 0x2,       ///< Needed shared libraries
  WASM_DYLINK_EXPORT_INFO = 0x3,  ///< Dynamic export info
  WASM_DYLINK_IMPORT_INFO = 0x4,  ///< Dynamic import info
  WASM_DYLINK_RUNTIME_PATH = 0x5, ///< Runtime search path
};

/// Kind codes used in the custom "linking" section in the WASM_COMDAT_INFO.
enum : unsigned {
  WASM_COMDAT_DATA = 0x0,     ///< Comdat entry for a data segment
  WASM_COMDAT_FUNCTION = 0x1, ///< Comdat entry for a function
  // GLOBAL, TAG, and TABLE are in here but LLVM doesn't use them yet.
  WASM_COMDAT_SECTION = 0x5,  ///< Comdat entry for a section
};

/// Kind codes used in the custom "linking" section in the WASM_SYMBOL_TABLE.
enum WasmSymbolType : unsigned {
  WASM_SYMBOL_TYPE_FUNCTION = 0x0, ///< Function symbol
  WASM_SYMBOL_TYPE_DATA = 0x1,     ///< Data symbol
  WASM_SYMBOL_TYPE_GLOBAL = 0x2,   ///< Global symbol
  WASM_SYMBOL_TYPE_SECTION = 0x3,  ///< Section symbol
  WASM_SYMBOL_TYPE_TAG = 0x4,      ///< Tag symbol
  WASM_SYMBOL_TYPE_TABLE = 0x5,    ///< Table symbol
};

/// Flags for data segments in the linking metadata.
enum WasmSegmentFlag : unsigned {
  WASM_SEG_FLAG_STRINGS = 0x1, ///< Segment contains null-terminated strings
  WASM_SEG_FLAG_TLS = 0x2,     ///< Segment is thread-local storage
  WASM_SEG_FLAG_RETAIN = 0x4,  ///< Segment must be retained by the linker
};

/// Kinds of tag attributes.
enum WasmTagAttribute : uint8_t {
  WASM_TAG_ATTRIBUTE_EXCEPTION = 0x0, ///< Exception tag attribute
};

/// Bitmask selecting the binding bits of a Wasm symbol's flags.
const unsigned WASM_SYMBOL_BINDING_MASK = 0x3;
/// Bitmask selecting the visibility bits of a Wasm symbol's flags.
const unsigned WASM_SYMBOL_VISIBILITY_MASK = 0xc;

/// Symbol has global (default) binding.
const unsigned WASM_SYMBOL_BINDING_GLOBAL = 0x0;
/// Symbol has weak binding.
const unsigned WASM_SYMBOL_BINDING_WEAK = 0x1;
/// Symbol has local binding.
const unsigned WASM_SYMBOL_BINDING_LOCAL = 0x2;
/// Symbol has common binding.
const unsigned WASM_SYMBOL_BINDING_COMMON = 0x3;
/// Symbol has default visibility.
const unsigned WASM_SYMBOL_VISIBILITY_DEFAULT = 0x0;
/// Symbol has hidden visibility.
const unsigned WASM_SYMBOL_VISIBILITY_HIDDEN = 0x4;
/// Symbol is undefined (imported).
const unsigned WASM_SYMBOL_UNDEFINED = 0x10;
/// Symbol must be exported from the final module.
const unsigned WASM_SYMBOL_EXPORTED = 0x20;
/// Symbol carries an explicitly recorded name.
const unsigned WASM_SYMBOL_EXPLICIT_NAME = 0x40;
/// Symbol must not be stripped by the linker.
const unsigned WASM_SYMBOL_NO_STRIP = 0x80;
/// Symbol is thread-local.
const unsigned WASM_SYMBOL_TLS = 0x100;
/// Symbol has an absolute address.
const unsigned WASM_SYMBOL_ABSOLUTE = 0x200;

#define WASM_RELOC(name, value) name = value,

/// Relocation types used in Wasm object files.
enum WasmRelocType : unsigned {
#include "WasmRelocs.def"
};

#undef WASM_RELOC

/// Header fields of a Wasm object file.
struct WasmObjectHeader {
  /// Magic string identifying a Wasm binary.
  StringRef Magic;
  /// Wasm binary format version.
  uint32_t Version;
};

/// Subset of types that a value can have.
enum class ValType {
  I32 = WASM_TYPE_I32,             ///< 32-bit integer
  I64 = WASM_TYPE_I64,             ///< 64-bit integer
  F32 = WASM_TYPE_F32,             ///< 32-bit float
  F64 = WASM_TYPE_F64,             ///< 64-bit float
  V128 = WASM_TYPE_V128,           ///< 128-bit SIMD vector
  FUNCREF = WASM_TYPE_FUNCREF,     ///< Function reference
  EXTERNREF = WASM_TYPE_EXTERNREF, ///< External reference
  EXNREF = WASM_TYPE_EXNREF,       ///< Exception reference
  /// Unmodeled reference or specialized funcref type.
  ///
  /// Unmodeled value types include ref types with heap types other than
  /// func, extern or exn, and type-specialized funcrefs.
  OTHERREF = 0xff,
};

/// Import entry from the custom "dylink" section.
struct WasmDylinkImportInfo {
  /// Module name of the import.
  StringRef Module;
  /// Field name of the import.
  StringRef Field;
  /// Symbol flags associated with the import.
  uint32_t Flags;
};

/// Export entry from the custom "dylink" section.
struct WasmDylinkExportInfo {
  /// Name of the export.
  StringRef Name;
  /// Symbol flags associated with the export.
  uint32_t Flags;
};

/// Contents of the custom "dylink" / "dylink.0" section.
struct WasmDylinkInfo {
  /// Memory size in bytes.
  uint32_t MemorySize;
  /// Power-of-two alignment of memory.
  uint32_t MemoryAlignment;
  /// Table size in elements.
  uint32_t TableSize;
  /// Power-of-two alignment of table.
  uint32_t TableAlignment;
  /// Shared library dependencies.
  std::vector<StringRef> Needed;
  /// Dynamic import metadata entries.
  std::vector<WasmDylinkImportInfo> ImportInfo;
  /// Dynamic export metadata entries.
  std::vector<WasmDylinkExportInfo> ExportInfo;
  /// Runtime library search paths.
  std::vector<StringRef> RuntimePath;
};

/// Producer metadata from the custom "producers" section.
struct WasmProducerInfo {
  /// Language name/version pairs that produced the module.
  std::vector<std::pair<std::string, std::string>> Languages;
  /// Tool name/version pairs that produced the module.
  std::vector<std::pair<std::string, std::string>> Tools;
  /// SDK name/version pairs that produced the module.
  std::vector<std::pair<std::string, std::string>> SDKs;
};

/// Feature requirement from the custom "target_features" section.
struct WasmFeatureEntry {
  /// Policy prefix (`+` used or `-` disallowed).
  uint8_t Prefix;
  /// Feature name string.
  std::string Name;
};

/// Export entry from the Wasm export section.
struct WasmExport {
  /// Exported name.
  StringRef Name;
  /// Kind of exported entity (function, table, memory, global, or tag).
  uint8_t Kind;
  /// Index of the exported entity in its index space.
  uint32_t Index;
};

/// Minimum/maximum limits for a table or memory.
struct WasmLimits {
  /// Limit flags (has-max, shared, is-64, has-page-size).
  uint8_t Flags;
  /// Minimum size (pages for memory, elements for tables).
  uint64_t Minimum;
  /// Maximum size when `WASM_LIMITS_FLAG_HAS_MAX` is set.
  uint64_t Maximum;
  /// Custom page size when `WASM_LIMITS_FLAG_HAS_PAGE_SIZE` is set.
  uint32_t PageSize;
};

/// Type of a Wasm table (element type and limits).
struct WasmTableType {
  /// Element reference type stored in the table.
  ValType ElemType;
  /// Minimum/maximum element counts for the table.
  WasmLimits Limits;
};

/// Table definition from the Wasm table section.
struct WasmTable {
  /// Index of this table in the table index space.
  uint32_t Index;
  /// Element type and limits of the table.
  WasmTableType Type;
  /// Symbol name from the "linking" section.
  StringRef SymbolName;
};

/// MVP-style initializer expression (single instruction plus operand).
struct WasmInitExprMVP {
  /// Opcode of the single initializer instruction.
  uint8_t Opcode;
  /// Immediate operand of the initializer instruction.
  union {
    int32_t Int32;    ///< i32.const operand
    int64_t Int64;    ///< i64.const operand
    uint32_t Float32; ///< f32.const bit pattern
    uint64_t Float64; ///< f64.const bit pattern
    uint32_t Global;  ///< global.get index
  } Value;
};

/// Initializer expression, including extended-const and GC forms.
///
/// Extended-const init exprs and exprs with GC types are not explicitly
/// modeled, but the raw body of the expr is attached.
struct WasmInitExpr {
  /// Non-zero when extended const is used (more than one instruction).
  uint8_t Extended;
  /// MVP single-instruction form when `Extended` is zero.
  WasmInitExprMVP Inst;
  /// Raw expression body bytes when extended or GC forms are used.
  ArrayRef<uint8_t> Body;
};

/// Type of a Wasm global (value type and mutability).
struct WasmGlobalType {
  /// Value type of the global (Wasm type encoding).
  ///
  /// TODO: make this a ValType?
  uint8_t Type;
  /// True if the global is mutable.
  bool Mutable;
};

/// Global definition from the Wasm global section.
struct WasmGlobal {
  /// Index of this global in the global index space.
  uint32_t Index;
  /// Value type and mutability of the global.
  WasmGlobalType Type;
  /// Initializer expression for the global.
  WasmInitExpr InitExpr;
  /// Symbol name from the "linking" section.
  StringRef SymbolName;
  /// Offset of the definition in the binary's Global section.
  uint32_t Offset;
  /// Size of the definition in the binary's Global section.
  uint32_t Size;
};

/// Tag definition from the Wasm tag section.
struct WasmTag {
  /// Index of this tag in the tag index space.
  uint32_t Index;
  /// Index of the tag's type signature.
  uint32_t SigIndex;
  /// Symbol name from the "linking" section.
  StringRef SymbolName;
};

/// Import entry from the Wasm import section.
struct WasmImport {
  /// Module name of the import.
  StringRef Module;
  /// Field name of the import.
  StringRef Field;
  /// Kind of imported entity.
  uint8_t Kind;
  /// Kind-specific payload for the import.
  union {
    uint32_t SigIndex;     ///< Type index for a function or tag import
    WasmGlobalType Global; ///< Type of a global import
    WasmTableType Table;   ///< Type of a table import
    WasmLimits Memory;     ///< Limits of a memory import
  };
};

/// Local variable declaration group within a function body.
struct WasmLocalDecl {
  /// Value type of locals in this group.
  uint8_t Type;
  /// Number of consecutive locals of this type.
  uint32_t Count;
};

/// Function definition from the Wasm function and code sections.
struct WasmFunction {
  /// Index of this function in the function index space.
  uint32_t Index;
  /// Index of this function's type signature.
  uint32_t SigIndex;
  /// Local variable declaration groups.
  std::vector<WasmLocalDecl> Locals;
  /// Raw bytes of the function body (opcodes after locals).
  ArrayRef<uint8_t> Body;
  /// Offset of this function within the code section.
  uint32_t CodeSectionOffset;
  /// Size in bytes of this function's code section entry.
  uint32_t Size;
  /// Offset to the start of Locals and Body within the function.
  uint32_t CodeOffset;
  /// Export name from the "export" section, if any.
  std::optional<StringRef> ExportName;
  /// Symbol name from the "linking" section.
  StringRef SymbolName;
  /// Debug name from the "name" section.
  StringRef DebugName;
  /// Comdat group index from the "comdat info" section.
  uint32_t Comdat;
};

/// Data segment from the Wasm data section (plus linking metadata).
struct WasmDataSegment {
  /// Encoding flags (passive and/or has memory index).
  uint32_t InitFlags;
  /// Memory index; present if InitFlags & WASM_DATA_SEGMENT_HAS_MEMINDEX.
  uint32_t MemoryIndex;
  /// Init offset; present if InitFlags & WASM_DATA_SEGMENT_IS_PASSIVE == 0.
  WasmInitExpr Offset;

  /// Raw bytes of the data segment contents.
  ArrayRef<uint8_t> Content;
  /// Segment name from the "segment info" section.
  StringRef Name;
  /// Alignment of the segment from linking metadata.
  uint32_t Alignment;
  /// Linking flags from the segment info subsection.
  uint32_t LinkingFlags;
  /// Comdat group index from the "comdat info" section.
  uint32_t Comdat;
};

/// Element segment modes encodable in the Wasm elem section.
///
/// Three different element segment modes are encodable. This enum is currently
/// only used during decoding (see WasmElemSegment below).
enum class ElemSegmentMode {
  Active,      ///< Active segment initialized into a table
  Passive,     ///< Passive segment retained for `table.init`
  Declarative, ///< Declarative segment for forward references
};

/// Wasm element segment, with some limitations compared the spec.
///
/// Limitations compared to the spec:
/// 1) Does not model passive or declarative segments (Segment will end up with
/// an Offset field of i32.const 0)
/// 2) Does not model init exprs (Segment will get an empty Functions list)
/// 3) Does not model types other than basic funcref/externref/exnref (see
/// ValType)
struct WasmElemSegment {
  /// Encoding flags for the element segment.
  uint32_t Flags;
  /// Destination table index for an active segment.
  uint32_t TableNumber;
  /// Element reference kind stored by the segment.
  ValType ElemKind;
  /// Offset expression into the destination table.
  WasmInitExpr Offset;
  /// Function indices used as element initializers.
  std::vector<uint32_t> Functions;
};

/// Location of a Wasm data symbol within a WasmDataSegment.
///
/// Represents the location as the index of the segment, and the offset and size
/// within the segment.
struct WasmDataReference {
  /// Index of the containing data segment.
  uint32_t Segment;
  /// Byte offset within the segment.
  uint64_t Offset;
  /// Size in bytes of the referenced range.
  uint64_t Size;
};

/// Size and alignment for a common (unallocated) data symbol.
struct WasmCommonReference {
  /// Size in bytes of the common symbol.
  uint64_t Size;
  /// Log2 alignment of the common symbol.
  uint32_t Alignment;
};

/// Relocation entry applied to a Wasm section.
struct WasmRelocation {
  /// The type of the relocation.
  uint8_t Type;
  /// Index into either symbol or type index space.
  uint32_t Index;
  /// Offset from the start of the section.
  uint64_t Offset;
  /// A value to add to the symbol.
  int64_t Addend;

  /// Return the relocation type as a `WasmRelocType` enumerator.
  ///
  /// \return The relocation type enumerator.
  WasmRelocType getType() const { return static_cast<WasmRelocType>(Type); }
};

/// Constructor function listed in the linking init-functions subsection.
struct WasmInitFunc {
  /// Priority used to order init function calls (lower runs first).
  uint32_t Priority;
  /// Symbol-table index of the init function.
  uint32_t Symbol;
};

/// Symbol-table entry from the custom "linking" section.
struct WasmSymbolInfo {
  /// Symbol name.
  StringRef Name;
  /// Symbol kind (`WasmSymbolType`).
  uint8_t Kind;
  /// Symbol flags (binding, visibility, undefined, TLS, and related bits).
  uint32_t Flags;
  /// For undefined symbols the module of the import.
  std::optional<StringRef> ImportModule;
  /// For undefined symbols the name of the import.
  std::optional<StringRef> ImportName;
  /// For symbols to be exported from the final module.
  std::optional<StringRef> ExportName;
  /// Kind-specific reference payload for the symbol.
  union {
    /// For function, table, or global symbols, the index in function, table, or
    /// global index space.
    uint32_t ElementIndex;
    /// For a data symbols, the address of the data relative to segment.
    WasmDataReference DataRef;
    /// For common symbols, the size and alignment.
    WasmCommonReference CommonRef;
  };
};

/// Kind of named entity recorded in Wasm debug/name metadata.
enum class NameType {
  FUNCTION,    ///< Function name
  GLOBAL,      ///< Global name
  DATA_SEGMENT, ///< Data-segment name
};

/// Debug name entry from the custom "name" section.
struct WasmDebugName {
  /// Kind of named entity.
  NameType Type;
  /// Index of the named entity in its index space.
  uint32_t Index;
  /// Debug name string.
  StringRef Name;
};

/// Info from the linking metadata section of a wasm object file.
///
/// The linking section also contains a symbol table. That info (represented in
/// a WasmSymbolInfo struct) is stored inside the WasmSymbol object instead of
/// in this structure; this allows vectors of WasmSymbols and WasmLinkingDatas
/// to be reallocated.
struct WasmLinkingData {
  /// Linking metadata version.
  uint32_t Version;
  /// Constructor functions to run at startup.
  std::vector<WasmInitFunc> InitFunctions;
  /// Comdat group names.
  std::vector<StringRef> Comdats;
};

/// Function or tag type signature from the Wasm type section.
///
/// LLVM can parse types other than functions encoded in the type section,
/// but does not actually model them. Instead a placeholder signature is
/// created in the Object's signature list.
struct WasmSignature {
  /// Result types of the signature.
  SmallVector<ValType, 1> Returns;
  /// Parameter types of the signature.
  SmallVector<ValType, 4> Params;
  /// Kind of entity this signature describes.
  enum {
    Function,    ///< Function type signature
    Tag,         ///< Tag type signature
    Placeholder  ///< Placeholder for an unmodeled type-section entry
  } Kind = Function;
  /// DenseMap empty-key state for this signature instance.
  ///
  /// Support empty instances, needed by DenseMap.
  enum {
    Plain, ///< Normal signature instance
    Empty  ///< Empty key used by DenseMap
  } State = Plain;

  /// Construct a signature from return and parameter type lists.
  ///
  /// \param InReturns Result types (moved into this signature).
  /// \param InParams Parameter types (moved into this signature).
  WasmSignature(SmallVector<ValType, 1> &&InReturns,
                SmallVector<ValType, 4> &&InParams)
      : Returns(InReturns), Params(InParams) {}
  /// Construct an empty signature with default kind and state.
  WasmSignature() = default;
};

/// Return whether two Wasm signatures compare equal.
///
/// \param LHS Left-hand signature.
/// \param RHS Right-hand signature.
/// \return True if the signatures compare equal.
inline bool operator==(const WasmSignature &LHS, const WasmSignature &RHS) {
  return LHS.State == RHS.State && LHS.Returns == RHS.Returns &&
         LHS.Params == RHS.Params;
}

/// Return whether two Wasm signatures compare unequal.
///
/// \param LHS Left-hand signature.
/// \param RHS Right-hand signature.
/// \return True if the signatures compare unequal.
inline bool operator!=(const WasmSignature &LHS, const WasmSignature &RHS) {
  return !(LHS == RHS);
}

/// Return whether two Wasm global types compare equal.
///
/// \param LHS Left-hand global type.
/// \param RHS Right-hand global type.
/// \return True if the global types compare equal.
inline bool operator==(const WasmGlobalType &LHS, const WasmGlobalType &RHS) {
  return LHS.Type == RHS.Type && LHS.Mutable == RHS.Mutable;
}

/// Return whether two Wasm global types compare unequal.
///
/// \param LHS Left-hand global type.
/// \param RHS Right-hand global type.
/// \return True if the global types compare unequal.
inline bool operator!=(const WasmGlobalType &LHS, const WasmGlobalType &RHS) {
  return !(LHS == RHS);
}

/// Return whether two Wasm limits compare equal.
///
/// \param LHS Left-hand limits.
/// \param RHS Right-hand limits.
/// \return True if the limits compare equal.
inline bool operator==(const WasmLimits &LHS, const WasmLimits &RHS) {
  return LHS.Flags == RHS.Flags && LHS.Minimum == RHS.Minimum &&
         (LHS.Flags & WASM_LIMITS_FLAG_HAS_MAX ? LHS.Maximum == RHS.Maximum
                                               : true) &&
         (LHS.Flags & WASM_LIMITS_FLAG_HAS_PAGE_SIZE
              ? LHS.PageSize == RHS.PageSize
              : true);
}

/// Return whether two Wasm table types compare equal.
///
/// \param LHS Left-hand table type.
/// \param RHS Right-hand table type.
/// \return True if the table types compare equal.
inline bool operator==(const WasmTableType &LHS, const WasmTableType &RHS) {
  return LHS.ElemType == RHS.ElemType && LHS.Limits == RHS.Limits;
}

/// Return the name of a Wasm symbol type enumerator.
///
/// \param type Symbol type to stringify.
/// \return String name of the symbol type.
LLVM_ABI llvm::StringRef toString(WasmSymbolType type);
/// Return the name of a Wasm relocation type value.
///
/// \param type Relocation type code to stringify.
/// \return String name of the relocation type.
LLVM_ABI llvm::StringRef relocTypetoString(uint32_t type);
/// Return the name of a Wasm section type value.
///
/// \param type Section id to stringify.
/// \return String name of the section type.
LLVM_ABI llvm::StringRef sectionTypeToString(uint32_t type);
/// Return whether the given relocation type carries an addend.
///
/// \param type Relocation type code to query.
/// \return True if the relocation type carries an addend.
LLVM_ABI bool relocTypeHasAddend(uint32_t type);

} // end namespace wasm
} // end namespace llvm

#endif
