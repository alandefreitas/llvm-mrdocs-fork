//===- PDBTypes.h - Defines enums for various fields contained in PDB ----====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBTYPES_H
#define LLVM_DEBUGINFO_PDB_PDBTYPES_H

#include "llvm/ADT/APFloat.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBFrameData.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>

namespace llvm {
namespace pdb {

/// Unique integer identifier of a symbol within a PDB session.
typedef uint32_t SymIndexId;

class IPDBDataStream;
class IPDBInjectedSource;
class IPDBLineNumber;
class IPDBSectionContrib;
class IPDBSession;
class IPDBSourceFile;
class IPDBTable;
class PDBSymDumper;
class PDBSymbol;
class PDBSymbolExe;
class PDBSymbolCompiland;
class PDBSymbolCompilandDetails;
class PDBSymbolCompilandEnv;
class PDBSymbolFunc;
class PDBSymbolBlock;
class PDBSymbolData;
class PDBSymbolAnnotation;
class PDBSymbolLabel;
class PDBSymbolPublicSymbol;
class PDBSymbolTypeUDT;
class PDBSymbolTypeEnum;
class PDBSymbolTypeFunctionSig;
class PDBSymbolTypePointer;
class PDBSymbolTypeArray;
class PDBSymbolTypeBuiltin;
class PDBSymbolTypeTypedef;
class PDBSymbolTypeBaseClass;
class PDBSymbolTypeFriend;
class PDBSymbolTypeFunctionArg;
class PDBSymbolFuncDebugStart;
class PDBSymbolFuncDebugEnd;
class PDBSymbolUsingNamespace;
class PDBSymbolTypeVTableShape;
class PDBSymbolTypeVTable;
class PDBSymbolCustom;
class PDBSymbolThunk;
class PDBSymbolTypeCustom;
class PDBSymbolTypeManaged;
class PDBSymbolTypeDimension;
class PDBSymbolUnknown;

/// Enumerator over PDB symbols.
using IPDBEnumSymbols = IPDBEnumChildren<PDBSymbol>;
/// Enumerator over source files referenced by a PDB.
using IPDBEnumSourceFiles = IPDBEnumChildren<IPDBSourceFile>;
/// Enumerator over named data streams in a PDB.
using IPDBEnumDataStreams = IPDBEnumChildren<IPDBDataStream>;
/// Enumerator over line-number entries.
using IPDBEnumLineNumbers = IPDBEnumChildren<IPDBLineNumber>;
/// Enumerator over tables exposed by a PDB session.
using IPDBEnumTables = IPDBEnumChildren<IPDBTable>;
/// Enumerator over injected source files embedded in a PDB.
using IPDBEnumInjectedSources = IPDBEnumChildren<IPDBInjectedSource>;
/// Enumerator over section contributions.
using IPDBEnumSectionContribs = IPDBEnumChildren<IPDBSectionContrib>;
/// Enumerator over frame data records.
using IPDBEnumFrameData = IPDBEnumChildren<IPDBFrameData>;

/// Specifies which PDB reader implementation is to be used.  Only a value
/// of PDB_ReaderType::DIA is currently supported, but Native is in the works.
enum class PDB_ReaderType {
  DIA = 0,    ///< Microsoft DIA SDK-backed PDB reader.
  Native = 1, ///< LLVM native PDB reader.
};

/// An enumeration indicating the type of data contained in this table.
enum class PDB_TableType {
  TableInvalid = 0,   ///< Invalid or unspecified table type.
  Symbols,            ///< Symbol table.
  SourceFiles,        ///< Source file table.
  LineNumbers,        ///< Line number table.
  SectionContribs,    ///< Section contribution table.
  Segments,           ///< Segment table.
  InjectedSources,    ///< Injected source file table.
  FrameData,          ///< Frame data table.
  InputAssemblyFiles, ///< Input assembly file table.
  Dbg                   ///< Debug stream table.
};

/// Flags used when enumerating child symbols by name.
///
/// Corresponds to the NameSearchOptions enumeration documented here:
/// https://msdn.microsoft.com/en-us/library/yat28ads.aspx
enum PDB_NameSearchFlags {
  NS_Default = 0x0,          ///< Default name search options.
  NS_CaseSensitive = 0x1,    ///< Match names case-sensitively.
  NS_CaseInsensitive = 0x2,  ///< Match names case-insensitively.
  NS_FileNameExtMatch = 0x4, ///< Match file name and extension only.
  NS_Regex = 0x8,            ///< Treat the search pattern as a regular expression.
  NS_UndecoratedName = 0x10, ///< Search using undecorated (demangled) names.

  // For backward compatibility.
  NS_CaseInFileNameExt = NS_CaseInsensitive | NS_FileNameExtMatch, ///< Case-insensitive filename+extension match.
  NS_CaseRegex = NS_Regex | NS_CaseSensitive,     ///< Case-sensitive regular expression match.
  NS_CaseInRex = NS_Regex | NS_CaseInsensitive    ///< Case-insensitive regular expression match.
};

/// Hash algorithm used for a source file checksum in a PDB.
///
/// Corresponds to the CV_SourceChksum_t enumeration documented here:
/// https://msdn.microsoft.com/en-us/library/e96az21x.aspx
enum class PDB_Checksum {
  None = 0,   ///< No checksum is recorded for the file.
  MD5 = 1,    ///< MD5 checksum.
  SHA1 = 2,   ///< SHA-1 checksum.
  SHA256 = 3  ///< SHA-256 checksum.
};

/// These values correspond to the CV_CPU_TYPE_e enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/b2fc64ek.aspx
using PDB_Cpu = codeview::CPUType;

/// Target machine architecture recorded in a PDB or PE image.
enum class PDB_Machine {
  Invalid = 0xffff, ///< Invalid or unrecognized machine type.
  Unknown = 0x0,    ///< Unknown machine type.
  Am33 = 0x13,      ///< Matsushita AM33.
  Amd64 = 0x8664,   ///< x64 (AMD64 / Intel 64).
  Arm = 0x1C0,      ///< ARM little-endian.
  Arm64 = 0xaa64,   ///< ARM64 little-endian.
  ArmNT = 0x1C4,    ///< ARM Thumb-2 little-endian.
  Ebc = 0xEBC,      ///< EFI byte code.
  x86 = 0x14C,      ///< Intel 386 or later (and compatible).
  Ia64 = 0x200,     ///< Intel Itanium.
  M32R = 0x9041,    ///< Mitsubishi M32R little-endian.
  Mips16 = 0x266,   ///< MIPS16.
  MipsFpu = 0x366,  ///< MIPS with FPU.
  MipsFpu16 = 0x466,///< MIPS16 with FPU.
  PowerPC = 0x1F0,  ///< PowerPC little-endian.
  PowerPCFP = 0x1F1,///< PowerPC with floating point.
  R4000 = 0x166,    ///< MIPS R4000 little-endian.
  SH3 = 0x1A2,      ///< Hitachi SH3.
  SH3DSP = 0x1A3,   ///< Hitachi SH3-DSP.
  SH4 = 0x1A6,      ///< Hitachi SH4.
  SH5 = 0x1A8,      ///< Hitachi SH5.
  Thumb = 0x1C2,    ///< ARM or Thumb ("interworking").
  WceMipsV2 = 0x169 ///< MIPS little-endian WCE v2.
};

// A struct with an inner unnamed enum with explicit underlying type resuls
// in an enum class that can implicitly convert to the underlying type, which
// is convenient for this enum.
/// Compression method used for an injected source file embedded in a PDB.
struct PDB_SourceCompression {
  /// Known compression encodings for injected PDB source contents.
  enum : uint32_t {
    /// No compression. Produced e.g. by `link.exe /natvis:foo.natvis`.
    None,
    /// Run-length encoded compression (producer unknown).
    RunLengthEncoded,
    /// Huffman compression (producer unknown).
    Huffman,
    /// LZ compression (producer unknown).
    LZ,
    /// .NET embedded source format produced e.g. by `csc /debug`.
    ///
    /// The encoded data is its own mini-stream with the following layout (in
    /// little endian):
    ///   GUID LanguageTypeGuid;
    ///   GUID LanguageVendorGuid;
    ///   GUID DocumentTypeGuid;
    ///   GUID HashFunctionGuid;
    ///   uint32_t HashDataSize;
    ///   uint32_t CompressedDataSize;
    /// Followed by HashDataSize bytes containing a hash checksum,
    /// followed by CompressedDataSize bytes containing source contents.
    ///
    /// CompressedDataSize can be 0, in this case only the hash data is present.
    /// (CompressedDataSize is != 0 e.g. if `/embed` is passed to csc.exe.)
    /// The compressed data format is:
    ///   uint32_t UncompressedDataSize;
    /// If UncompressedDataSize is 0, the data is stored uncompressed and
    /// CompressedDataSize stores the uncompressed size.
    /// If UncompressedDataSize is != 0, then the data is in raw deflate
    /// encoding as described in rfc1951.
    ///
    /// A GUID is 16 bytes, stored in the usual
    ///   uint32_t
    ///   uint16_t
    ///   uint16_t
    ///   uint8_t[24]
    /// layout.
    ///
    /// Well-known GUIDs for LanguageTypeGuid are:
    ///   63a08714-fc37-11d2-904c-00c04fa302a1 C
    ///   3a12d0b7-c26c-11d0-b442-00a0244a1dd2 C++
    ///   3f5162f8-07c6-11d3-9053-00c04fa302a1 C#
    ///   af046cd1-d0e1-11d2-977c-00a0c9b4d50c Cobol
    ///   ab4f38c9-b6e6-43ba-be3b-58080b2ccce3 F#
    ///   3a12d0b4-c26c-11d0-b442-00a0244a1dd2 Java
    ///   3a12d0b6-c26c-11d0-b442-00a0244a1dd2 JScript
    ///   af046cd2-d0e1-11d2-977c-00a0c9b4d50c Pascal
    ///   3a12d0b8-c26c-11d0-b442-00a0244a1dd2 Visual Basic
    ///
    /// Well-known GUIDs for LanguageVendorGuid are:
    ///   994b45c4-e6e9-11d2-903f-00c04fa302a1 Microsoft
    ///
    /// Well-known GUIDs for DocumentTypeGuid are:
    ///   5a869d0b-6611-11d3-bd2a-0000f80849bd Text
    ///
    /// Well-known GUIDs for HashFunctionGuid are:
    ///   406ea660-64cf-4c82-b6f0-42d48172a799 MD5    (HashDataSize is 16)
    ///   ff1816ec-aa5e-4d10-87f7-6f4963833460 SHA1   (HashDataSize is 20)
    ///   8829d00f-11b8-4213-878b-770e8597ac16 SHA256 (HashDataSize is 32)
    DotNet = 101,
  };
};

/// Calling convention of a function type.
///
/// These values correspond to the CV_call_e enumeration, and are documented
/// at the following locations:
///   https://msdn.microsoft.com/en-us/library/b2fc64ek.aspx
///   https://msdn.microsoft.com/en-us/library/windows/desktop/ms680207(v=vs.85).aspx
using PDB_CallingConv = codeview::CallingConvention;

/// These values correspond to the CV_CFL_LANG enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/bw3aekw6.aspx
using PDB_Lang = codeview::SourceLanguage;

/// These values correspond to the DataKind enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/b2x2t313.aspx
enum class PDB_DataKind {
  Unknown,      ///< Unknown or unspecified data kind.
  Local,        ///< Local variable.
  StaticLocal,  ///< Static local variable.
  Param,        ///< Function parameter.
  ObjectPtr,    ///< Object pointer (e.g. this / self).
  FileStatic,   ///< File-scope static variable.
  Global,       ///< Global variable.
  Member,       ///< Non-static data member.
  StaticMember, ///< Static data member.
  Constant      ///< Constant value.
};

/// These values correspond to the SymTagEnum enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/bkedss5f.aspx
enum class PDB_SymType {
  None,               ///< No symbol tag / unknown.
  Exe,                ///< Executable or DLL image.
  Compiland,          ///< Compilation unit (compiland).
  CompilandDetails,   ///< Extra details about a compiland.
  CompilandEnv,       ///< Compiland environment variable.
  Function,           ///< Function or method.
  Block,              ///< Lexical block.
  Data,               ///< Data symbol.
  Annotation,         ///< Annotation symbol.
  Label,              ///< Code label.
  PublicSymbol,       ///< Public symbol.
  UDT,                ///< User-defined type (class/struct/union).
  Enum,               ///< Enumeration type.
  FunctionSig,        ///< Function signature / type.
  PointerType,        ///< Pointer type.
  ArrayType,          ///< Array type.
  BuiltinType,        ///< Built-in (basic) type.
  Typedef,            ///< Typedef.
  BaseClass,          ///< Base class.
  Friend,             ///< Friend declaration.
  FunctionArg,        ///< Function argument type.
  FuncDebugStart,      ///< Start of function prologue debug range.
  FuncDebugEnd,        ///< End of function prologue debug range.
  UsingNamespace,     ///< Using-namespace directive.
  VTableShape,        ///< Virtual table shape.
  VTable,             ///< Virtual table.
  Custom,             ///< Custom (vendor) symbol.
  Thunk,              ///< Thunk.
  CustomType,         ///< Custom (vendor) type.
  ManagedType,        ///< Managed type.
  Dimension,          ///< Array or matrix dimension.
  CallSite,           ///< Indirect call site.
  InlineSite,         ///< Inlined call site.
  BaseInterface,      ///< Base interface.
  VectorType,         ///< Vector type.
  MatrixType,         ///< Matrix type.
  HLSLType,           ///< HLSL type.
  Caller,             ///< Caller of a function.
  Callee,             ///< Callee of a function.
  Export,             ///< Export symbol.
  HeapAllocationSite, ///< Heap allocation site.
  CoffGroup,          ///< COFF group.
  Inlinee,            ///< Inlinee function.
  Max                 ///< Sentinel one past the last valid tag.
};

/// These values correspond to the LocationType enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/f57kaez3.aspx
enum class PDB_LocType {
  Null,             ///< No location / unknown.
  Static,           ///< Static storage (section + offset).
  TLS,              ///< Thread-local storage.
  RegRel,           ///< Register-relative address.
  ThisRel,          ///< Offset from this pointer.
  Enregistered,     ///< Value held in a register.
  BitField,         ///< Bit-field location.
  Slot,             ///< Managed slot.
  IlRel,            ///< IL-relative location.
  MetaData,         ///< Metadata token location.
  Constant,         ///< Constant value location.
  RegRelAliasIndir, ///< Register-relative with aliasing indirection.
  Max               ///< Sentinel one past the last valid location type.
};

/// These values correspond to the UdtKind enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/wcstk66t.aspx
enum class PDB_UdtType {
  Struct,    ///< Structure type.
  Class,     ///< Class type.
  Union,     ///< Union type.
  Interface  ///< Interface type.
};

/// These values correspond to the StackFrameTypeEnum enumeration, and are
/// documented here: https://msdn.microsoft.com/en-us/library/bc5207xw.aspx.
enum class PDB_StackFrameType : uint16_t {
  FPO,              ///< Frame pointer omitted (FPO) frame.
  KernelTrap,       ///< Kernel trap frame.
  KernelTSS,        ///< Kernel TSS frame.
  EBP,              ///< EBP-based frame.
  FrameData,        ///< Frame described by FrameData records.
  Unknown = 0xffff  ///< Unknown stack frame type.
};

/// These values correspond to the MemoryTypeEnum enumeration, and are
/// documented here: https://msdn.microsoft.com/en-us/library/ms165609.aspx.
enum class PDB_MemoryType : uint16_t {
  Code,           ///< Code memory.
  Data,           ///< Data memory.
  Stack,          ///< Stack memory.
  HeapCode,       ///< Heap-allocated code memory.
  Any = 0xffff    ///< Any memory type.
};

/// These values correspond to the Basictype enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/4szdtzc3.aspx
enum class PDB_BuiltinType {
  None = 0,      ///< No type / unknown.
  Void = 1,      ///< void.
  Char = 2,      ///< Character (char).
  WCharT = 3,    ///< Wide character (wchar_t).
  Int = 6,       ///< Signed integer.
  UInt = 7,      ///< Unsigned integer.
  Float = 8,     ///< Floating-point type.
  BCD = 9,       ///< Binary-coded decimal.
  Bool = 10,     ///< Boolean.
  Long = 13,     ///< Signed long integer.
  ULong = 14,    ///< Unsigned long integer.
  Currency = 25, ///< Currency type.
  Date = 26,     ///< Date type.
  Variant = 27,  ///< VARIANT type.
  Complex = 28,  ///< Complex floating-point type.
  Bitfield = 29, ///< Bit-field type.
  BSTR = 30,     ///< COM BSTR string.
  HResult = 31,  ///< HRESULT.
  Char16 = 32,   ///< char16_t.
  Char32 = 33,   ///< char32_t.
  Char8 = 34,    ///< char8_t.
};

/// Flags controlling how C++ decorated names are undecorated.
///
/// These values correspond to the flags that can be combined to control the
/// return of an undecorated name for a C++ decorated name, and are documented
/// here: https://msdn.microsoft.com/en-us/library/kszfk0fs.aspx
enum PDB_UndnameFlags : uint32_t {
  Undname_Complete = 0x0,              ///< Enable full undecoration.
  Undname_NoLeadingUnderscores = 0x1,  ///< Remove leading underscores from Microsoft keywords.
  Undname_NoMsKeywords = 0x2,          ///< Disable expansion of Microsoft keywords.
  Undname_NoFuncReturns = 0x4,         ///< Disable return-type expansion for functions.
  Undname_NoAllocModel = 0x8,          ///< Disable declaration of calling models.
  Undname_NoAllocLang = 0x10,          ///< Disable declaration of target language-specific conventions.
  Undname_Reserved1 = 0x20,            ///< Reserved.
  Undname_Reserved2 = 0x40,            ///< Reserved.
  Undname_NoThisType = 0x60,           ///< Disable `this` pointer type expansion.
  Undname_NoAccessSpec = 0x80,         ///< Disable access-specifier keywords.
  Undname_NoThrowSig = 0x100,          ///< Disable throw-signature expansion.
  Undname_NoMemberType = 0x200,        ///< Disable member-type (static/virtual) expansion.
  Undname_NoReturnUDTModel = 0x400,    ///< Disable Microsoft return UDT model keywords.
  Undname_32BitDecode = 0x800,         ///< Undecorate using 32-bit rules.
  Undname_NameOnly = 0x1000,           ///< Return only the undecorated name.
  Undname_TypeOnly = 0x2000,           ///< Return only the type of the primary declaration.
  Undname_HaveParams = 0x4000,         ///< Include function parameter types.
  Undname_NoECSU = 0x8000,             ///< Suppress enum/class/struct/union prefixes.
  Undname_NoIdentCharCheck = 0x10000,  ///< Disable validity checks on identifier characters.
  Undname_NoPTR64 = 0x20000            ///< Disable `__ptr64` expansion.
};

/// Access level of a class or struct member.
enum class PDB_MemberAccess {
  Private = 1,   ///< Private member access.
  Protected = 2, ///< Protected member access.
  Public = 3     ///< Public member access.
};

/// Version number of a PDB or related component.
struct VersionInfo {
  uint32_t Major; ///< Major version number.
  uint32_t Minor; ///< Minor version number.
  uint32_t Build; ///< Build number.
  uint32_t QFE;   ///< Quick-fix engineering (QFE) / patch number.
};

/// Discriminator for the active payload stored in a \c Variant.
enum PDB_VariantType {
  Empty,   ///< No value stored.
  Unknown, ///< Unknown or unsupported variant type.
  Int8,    ///< Signed 8-bit integer.
  Int16,   ///< Signed 16-bit integer.
  Int32,   ///< Signed 32-bit integer.
  Int64,   ///< Signed 64-bit integer.
  Single,  ///< 32-bit floating-point value.
  Double,  ///< 64-bit floating-point value.
  UInt8,   ///< Unsigned 8-bit integer.
  UInt16,  ///< Unsigned 16-bit integer.
  UInt32,  ///< Unsigned 32-bit integer.
  UInt64,  ///< Unsigned 64-bit integer.
  Bool,    ///< Boolean value.
  String   ///< Null-terminated character string.
};

/// Tagged union holding a scalar PDB constant value.
struct Variant {
  /// Construct an empty variant with type Empty.
  Variant() = default;

  /// Construct a boolean variant.
  /// \param V Boolean value to store.
  explicit Variant(bool V) : Type(PDB_VariantType::Bool) { Value.Bool = V; }
  /// Construct a signed 8-bit integer variant.
  /// \param V Value to store.
  explicit Variant(int8_t V) : Type(PDB_VariantType::Int8) { Value.Int8 = V; }
  /// Construct a signed 16-bit integer variant.
  /// \param V Value to store.
  explicit Variant(int16_t V) : Type(PDB_VariantType::Int16) {
    Value.Int16 = V;
  }
  /// Construct a signed 32-bit integer variant.
  /// \param V Value to store.
  explicit Variant(int32_t V) : Type(PDB_VariantType::Int32) {
    Value.Int32 = V;
  }
  /// Construct a signed 64-bit integer variant.
  /// \param V Value to store.
  explicit Variant(int64_t V) : Type(PDB_VariantType::Int64) {
    Value.Int64 = V;
  }
  /// Construct a 32-bit floating-point variant.
  /// \param V Value to store.
  explicit Variant(float V) : Type(PDB_VariantType::Single) {
    Value.Single = V;
  }
  /// Construct a 64-bit floating-point variant.
  /// \param V Value to store.
  explicit Variant(double V) : Type(PDB_VariantType::Double) {
    Value.Double = V;
  }
  /// Construct an unsigned 8-bit integer variant.
  /// \param V Value to store.
  explicit Variant(uint8_t V) : Type(PDB_VariantType::UInt8) {
    Value.UInt8 = V;
  }
  /// Construct an unsigned 16-bit integer variant.
  /// \param V Value to store.
  explicit Variant(uint16_t V) : Type(PDB_VariantType::UInt16) {
    Value.UInt16 = V;
  }
  /// Construct an unsigned 32-bit integer variant.
  /// \param V Value to store.
  explicit Variant(uint32_t V) : Type(PDB_VariantType::UInt32) {
    Value.UInt32 = V;
  }
  /// Construct an unsigned 64-bit integer variant.
  /// \param V Value to store.
  explicit Variant(uint64_t V) : Type(PDB_VariantType::UInt64) {
    Value.UInt64 = V;
  }

  /// Copy-construct a variant, deep-copying string payloads.
  /// \param Other Variant to copy.
  Variant(const Variant &Other) {
    *this = Other;
  }

  /// Destroy the variant, freeing any owned string payload.
  ~Variant() {
    if (Type == PDB_VariantType::String)
      delete[] Value.String;
  }

  /// Discriminator indicating which member of \c Value is active.
  PDB_VariantType Type = PDB_VariantType::Empty;
  /// Storage for the active variant payload.
  union {
    /// Boolean payload when \c Type is Bool.
    bool Bool;
    /// Signed 8-bit integer payload.
    int8_t Int8;
    /// Signed 16-bit integer payload.
    int16_t Int16;
    /// Signed 32-bit integer payload.
    int32_t Int32;
    /// Signed 64-bit integer payload.
    int64_t Int64;
    /// 32-bit floating-point payload.
    float Single;
    /// 64-bit floating-point payload.
    double Double;
    /// Unsigned 8-bit integer payload.
    uint8_t UInt8;
    /// Unsigned 16-bit integer payload.
    uint16_t UInt16;
    /// Unsigned 32-bit integer payload.
    uint32_t UInt32;
    /// Unsigned 64-bit integer payload.
    uint64_t UInt64;
    /// Owned null-terminated string payload when \c Type is String.
    char *String;
  } Value;

  /// Return true when the active type is an integral or boolean value.
  /// \return True if the active type is integral or boolean.
  bool isIntegralType() const {
    switch (Type) {
    case Bool:
    case Int8:
    case Int16:
    case Int32:
    case Int64:
    case UInt8:
    case UInt16:
    case UInt32:
    case UInt64:
      return true;
    default:
      return false;
    }
  }

#define VARIANT_WIDTH(Enum, NumBits)                                           \
  case PDB_VariantType::Enum:                                                  \
    return NumBits;

  /// Return the bit width of the active numeric payload.
  /// \return Bit width of the active numeric payload, or 0 for non-numeric types.
  unsigned getBitWidth() const {
    switch (Type) {
      VARIANT_WIDTH(Bool, 1u)
      VARIANT_WIDTH(Int8, 8u)
      VARIANT_WIDTH(Int16, 16u)
      VARIANT_WIDTH(Int32, 32u)
      VARIANT_WIDTH(Int64, 64u)
      VARIANT_WIDTH(Single, 32u)
      VARIANT_WIDTH(Double, 64u)
      VARIANT_WIDTH(UInt8, 8u)
      VARIANT_WIDTH(UInt16, 16u)
      VARIANT_WIDTH(UInt32, 32u)
      VARIANT_WIDTH(UInt64, 64u)
    default:
      assert(false && "Variant::toAPSInt called on non-numeric type");
      return 0u;
    }
  }

#undef VARIANT_WIDTH

#define VARIANT_APSINT(Enum, NumBits, IsUnsigned)                              \
  case PDB_VariantType::Enum:                                                  \
    return APSInt(                                                             \
        APInt(NumBits, static_cast<uint64_t>(Value.Enum), !IsUnsigned),        \
        IsUnsigned);

  /// Convert the active integral payload to an \c APSInt.
  /// \return The integral payload as an \c APSInt.
  APSInt toAPSInt() const {
    switch (Type) {
      VARIANT_APSINT(Bool, 1u, true)
      VARIANT_APSINT(Int8, 8u, false)
      VARIANT_APSINT(Int16, 16u, false)
      VARIANT_APSINT(Int32, 32u, false)
      VARIANT_APSINT(Int64, 64u, false)
      VARIANT_APSINT(UInt8, 8u, true)
      VARIANT_APSINT(UInt16, 16u, true)
      VARIANT_APSINT(UInt32, 32u, true)
      VARIANT_APSINT(UInt64, 64u, true)
    default:
      assert(false && "Variant::toAPSInt called on non-integral type");
      return APSInt();
    }
  }

#undef VARIANT_APSINT

  /// Convert the active floating-point payload to an \c APFloat.
  /// \return The floating-point payload as an \c APFloat.
  APFloat toAPFloat() const {
    // Float constants may be tagged as integers.
    switch (Type) {
    case PDB_VariantType::Single:
    case PDB_VariantType::UInt32:
    case PDB_VariantType::Int32:
      return APFloat(Value.Single);
    case PDB_VariantType::Double:
    case PDB_VariantType::UInt64:
    case PDB_VariantType::Int64:
      return APFloat(Value.Double);
    default:
      assert(false && "Variant::toAPFloat called on non-floating-point type");
      return APFloat::getZero(APFloat::IEEEsingle());
    }
  }

#define VARIANT_EQUAL_CASE(Enum)                                               \
  case PDB_VariantType::Enum:                                                  \
    return Value.Enum == Other.Value.Enum;

  /// Return true when this variant has the same type and value as \p Other.
  /// \param Other Variant to compare against.
  /// \return True if both variants have the same type and value.
  bool operator==(const Variant &Other) const {
    if (Type != Other.Type)
      return false;
    switch (Type) {
      VARIANT_EQUAL_CASE(Bool)
      VARIANT_EQUAL_CASE(Int8)
      VARIANT_EQUAL_CASE(Int16)
      VARIANT_EQUAL_CASE(Int32)
      VARIANT_EQUAL_CASE(Int64)
      VARIANT_EQUAL_CASE(Single)
      VARIANT_EQUAL_CASE(Double)
      VARIANT_EQUAL_CASE(UInt8)
      VARIANT_EQUAL_CASE(UInt16)
      VARIANT_EQUAL_CASE(UInt32)
      VARIANT_EQUAL_CASE(UInt64)
      VARIANT_EQUAL_CASE(String)
    default:
      return true;
    }
  }

#undef VARIANT_EQUAL_CASE

  /// Return true when this variant differs from \p Other.
  /// \param Other Variant to compare against.
  /// \return True if the variants differ in type or value.
  bool operator!=(const Variant &Other) const { return !(*this == Other); }
  /// Copy-assign from \p Other, deep-copying string payloads.
  /// \param Other Variant to assign from.
  /// \return Reference to this variant.
  Variant &operator=(const Variant &Other) {
    if (this == &Other)
      return *this;
    if (Type == PDB_VariantType::String)
      delete[] Value.String;
    Type = Other.Type;
    Value = Other.Value;
    if (Other.Type == PDB_VariantType::String &&
        Other.Value.String != nullptr) {
      Value.String = new char[strlen(Other.Value.String) + 1];
      ::strcpy(Value.String, Other.Value.String);
    }
    return *this;
  }
};

} // end namespace pdb
} // end namespace llvm

template <> struct std::hash<llvm::pdb::PDB_SymType> {
  std::size_t operator()(const llvm::pdb::PDB_SymType &Arg) const {
    return std::hash<int>()(static_cast<int>(Arg));
  }
};

#endif // LLVM_DEBUGINFO_PDB_PDBTYPES_H
