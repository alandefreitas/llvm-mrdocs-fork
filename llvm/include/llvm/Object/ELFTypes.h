//===- ELFTypes.h - Endian specific types for ELF ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ELFTYPES_H
#define LLVM_OBJECT_ELFTYPES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/BBAddrMap.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace llvm {

/// Types and flags for ELF call-graph profile section entries.
namespace callgraph {
/// Bitwise NOT for bitmask enumerations in this namespace.
using ::llvm::BitmaskEnumDetail::operator~;
/// Bitwise OR for bitmask enumerations in this namespace.
using ::llvm::BitmaskEnumDetail::operator|;
/// Bitwise AND for bitmask enumerations in this namespace.
using ::llvm::BitmaskEnumDetail::operator&;
/// Bitwise XOR for bitmask enumerations in this namespace.
using ::llvm::BitmaskEnumDetail::operator^;
/// Left shift for bitmask enumerations in this namespace.
using ::llvm::BitmaskEnumDetail::operator<<;
/// Right shift for bitmask enumerations in this namespace.
using ::llvm::BitmaskEnumDetail::operator>>;
/// In-place bitwise OR for bitmask enumerations in this namespace.
using ::llvm::BitmaskEnumDetail::operator|=;
/// In-place bitwise AND for bitmask enumerations in this namespace.
using ::llvm::BitmaskEnumDetail::operator&=;
/// In-place bitwise XOR for bitmask enumerations in this namespace.
using ::llvm::BitmaskEnumDetail::operator^=;
/// In-place left shift for bitmask enumerations in this namespace.
using ::llvm::BitmaskEnumDetail::operator<<=;
/// In-place right shift for bitmask enumerations in this namespace.
using ::llvm::BitmaskEnumDetail::operator>>=;
/// True when no bits are set in a bitmask enumeration value.
using ::llvm::BitmaskEnumDetail::operator!;
/// True when any bit is set in a bitmask enumeration value.
using ::llvm::BitmaskEnumDetail::any;

/// Bit flags stored in an ELF call-graph section entry.
enum Flags : uint8_t {
  /// No flags set.
  None = 0,
  /// The symbol is an indirect-call target.
  IsIndirectTarget = 1u << 0,
  /// The function has known direct callees.
  HasDirectCallees = 1u << 1,
  /// The function has known indirect callees.
  HasIndirectCallees = 1u << 2,
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/HasIndirectCallees)
};
} // namespace callgraph

namespace object {

template <class ELFT> struct Elf_Ehdr_Impl;
template <class ELFT> struct Elf_Shdr_Impl;
template <class ELFT> struct Elf_Sym_Impl;
template <class ELFT> struct Elf_Dyn_Impl;
/// ELF program header (\c Elf32_Phdr / \c Elf64_Phdr).
template <class ELFT> struct Elf_Phdr_Impl;
/// ELF relocation entry (\c Rel or \c Rela, 32- or 64-bit).
template <class ELFT, bool isRela> struct Elf_Rel_Impl;
template <bool Is64> struct Elf_Crel_Impl;
template <class ELFT> struct Elf_Verdef_Impl;
template <class ELFT> struct Elf_Verdaux_Impl;
template <class ELFT> struct Elf_Verneed_Impl;
template <class ELFT> struct Elf_Vernaux_Impl;
template <class ELFT> struct Elf_Versym_Impl;
template <class ELFT> struct Elf_Hash_Impl;
template <class ELFT> struct Elf_GnuHash_Impl;
/// ELF compression header (\c Elf32_Chdr / \c Elf64_Chdr).
template <class ELFT> struct Elf_Chdr_Impl;
template <class ELFT> struct Elf_Nhdr_Impl;
template <class ELFT> class Elf_Note_Impl;
template <class ELFT> class Elf_Note_Iterator_Impl;
template <class ELFT> struct Elf_CGProfile_Impl;

/// Endian- and width-specific bundle of ELF integer and structure types.
///
/// Instantiated as \c ELF32LE, \c ELF32BE, \c ELF64LE, or \c ELF64BE. Nested
/// aliases name packed integers and the corresponding header/entry types.
template <endianness E, bool Is64> struct ELFType {
private:
  template <typename Ty>
  using packed = support::detail::packed_endian_specific_integral<Ty, E, 1>;

public:
  /// Byte order of this ELF encoding.
  static const endianness Endianness = E;
  /// True when this encoding is ELFCLASS64.
  static const bool Is64Bits = Is64;

  /// Unsigned address-width integer (32 or 64 bits).
  using uint = std::conditional_t<Is64, uint64_t, uint32_t>;
  /// ELF file header type.
  using Ehdr = Elf_Ehdr_Impl<ELFType<E, Is64>>;
  /// ELF section header type.
  using Shdr = Elf_Shdr_Impl<ELFType<E, Is64>>;
  /// ELF symbol table entry type.
  using Sym = Elf_Sym_Impl<ELFType<E, Is64>>;
  /// ELF dynamic table entry type.
  using Dyn = Elf_Dyn_Impl<ELFType<E, Is64>>;
  /// ELF program header type.
  using Phdr = Elf_Phdr_Impl<ELFType<E, Is64>>;
  /// ELF relocation without an explicit addend.
  using Rel = Elf_Rel_Impl<ELFType<E, Is64>, false>;
  /// ELF relocation with an explicit addend.
  using Rela = Elf_Rel_Impl<ELFType<E, Is64>, true>;
  /// Compact ELF relocation (CREL) in-memory representation.
  using Crel = Elf_Crel_Impl<Is64>;
  /// Packed RELR relocation bitmap word.
  using Relr = packed<uint>;
  /// GNU version definition entry.
  using Verdef = Elf_Verdef_Impl<ELFType<E, Is64>>;
  /// GNU version definition auxiliary entry.
  using Verdaux = Elf_Verdaux_Impl<ELFType<E, Is64>>;
  /// GNU version need entry.
  using Verneed = Elf_Verneed_Impl<ELFType<E, Is64>>;
  /// GNU version need auxiliary entry.
  using Vernaux = Elf_Vernaux_Impl<ELFType<E, Is64>>;
  /// GNU symbol version index entry.
  using Versym = Elf_Versym_Impl<ELFType<E, Is64>>;
  /// SysV \c .hash section header.
  using Hash = Elf_Hash_Impl<ELFType<E, Is64>>;
  /// GNU \c .gnu.hash section header.
  using GnuHash = Elf_GnuHash_Impl<ELFType<E, Is64>>;
  /// ELF compression header.
  using Chdr = Elf_Chdr_Impl<ELFType<E, Is64>>;
  /// ELF note header.
  using Nhdr = Elf_Nhdr_Impl<ELFType<E, Is64>>;
  /// ELF note view wrapping a note header.
  using Note = Elf_Note_Impl<ELFType<E, Is64>>;
  /// Forward iterator over ELF notes.
  using NoteIterator = Elf_Note_Iterator_Impl<ELFType<E, Is64>>;
  /// Call-graph profile section entry.
  using CGProfile = Elf_CGProfile_Impl<ELFType<E, Is64>>;
  /// Contiguous range of dynamic table entries.
  using DynRange = ArrayRef<Dyn>;
  /// Contiguous range of section headers.
  using ShdrRange = ArrayRef<Shdr>;
  /// Contiguous range of symbol table entries.
  using SymRange = ArrayRef<Sym>;
  /// Contiguous range of Rel relocations.
  using RelRange = ArrayRef<Rel>;
  /// Contiguous range of Rela relocations.
  using RelaRange = ArrayRef<Rela>;
  /// Contiguous range of RELR bitmap words.
  using RelrRange = ArrayRef<Relr>;
  /// Contiguous range of program headers.
  using PhdrRange = ArrayRef<Phdr>;

  /// Packed 16-bit unsigned ELF half-word.
  using Half = packed<uint16_t>;
  /// Packed 32-bit unsigned ELF word.
  using Word = packed<uint32_t>;
  /// Packed 32-bit signed ELF word.
  using Sword = packed<int32_t>;
  /// Packed 64-bit unsigned ELF word.
  using Xword = packed<uint64_t>;
  /// Packed 64-bit signed ELF word.
  using Sxword = packed<int64_t>;
  /// Packed ELF virtual address.
  using Addr = packed<uint>;
  /// Packed ELF file offset.
  using Off = packed<uint>;
};

/// Little-endian 32-bit ELF type bundle.
using ELF32LE = ELFType<llvm::endianness::little, false>;
/// Big-endian 32-bit ELF type bundle.
using ELF32BE = ELFType<llvm::endianness::big, false>;
/// Little-endian 64-bit ELF type bundle.
using ELF64LE = ELFType<llvm::endianness::little, true>;
/// Big-endian 64-bit ELF type bundle.
using ELF64BE = ELFType<llvm::endianness::big, true>;

// Use an alignment of 2 for the typedefs since that is the worst case for
// ELF files in archives.

// I really don't like doing this, but the alternative is copypasta.
#define LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)                                       \
  /** Packed ELF virtual address. */                                           \
  using Elf_Addr = typename ELFT::Addr;                                        \
  /** Packed ELF file offset. */                                               \
  using Elf_Off = typename ELFT::Off;                                          \
  /** Packed 16-bit unsigned ELF half-word. */                                 \
  using Elf_Half = typename ELFT::Half;                                        \
  /** Packed 32-bit unsigned ELF word. */                                      \
  using Elf_Word = typename ELFT::Word;                                        \
  /** Packed 32-bit signed ELF word. */                                        \
  using Elf_Sword = typename ELFT::Sword;                                      \
  /** Packed 64-bit unsigned ELF word. */                                      \
  using Elf_Xword = typename ELFT::Xword;                                      \
  /** Packed 64-bit signed ELF word. */                                        \
  using Elf_Sxword = typename ELFT::Sxword;                                    \
  /** Unsigned address-width integer (32 or 64 bits). */                       \
  using uintX_t = typename ELFT::uint;                                         \
  /** ELF file header type. */                                                 \
  using Elf_Ehdr = typename ELFT::Ehdr;                                        \
  /** ELF section header type. */                                              \
  using Elf_Shdr = typename ELFT::Shdr;                                        \
  /** ELF symbol table entry type. */                                          \
  using Elf_Sym = typename ELFT::Sym;                                          \
  /** ELF dynamic table entry type. */                                         \
  using Elf_Dyn = typename ELFT::Dyn;                                          \
  /** ELF program header type. */                                              \
  using Elf_Phdr = typename ELFT::Phdr;                                        \
  /** ELF relocation without an explicit addend. */                            \
  using Elf_Rel = typename ELFT::Rel;                                          \
  /** ELF relocation with an explicit addend. */                               \
  using Elf_Rela = typename ELFT::Rela;                                        \
  /** Compact ELF relocation (CREL) in-memory representation. */               \
  using Elf_Crel = typename ELFT::Crel;                                        \
  /** Packed RELR relocation bitmap word. */                                   \
  using Elf_Relr = typename ELFT::Relr;                                        \
  /** GNU version definition entry. */                                         \
  using Elf_Verdef = typename ELFT::Verdef;                                    \
  /** GNU version definition auxiliary entry. */                               \
  using Elf_Verdaux = typename ELFT::Verdaux;                                  \
  /** GNU version need entry. */                                               \
  using Elf_Verneed = typename ELFT::Verneed;                                  \
  /** GNU version need auxiliary entry. */                                     \
  using Elf_Vernaux = typename ELFT::Vernaux;                                  \
  /** GNU symbol version index entry. */                                       \
  using Elf_Versym = typename ELFT::Versym;                                    \
  /** SysV .hash section header. */                                            \
  using Elf_Hash = typename ELFT::Hash;                                        \
  /** GNU .gnu.hash section header. */                                         \
  using Elf_GnuHash = typename ELFT::GnuHash;                                  \
  /** ELF compression header. */                                               \
  using Elf_Chdr = typename ELFT::Chdr;                                        \
  /** ELF note header. */                                                      \
  using Elf_Nhdr = typename ELFT::Nhdr;                                        \
  /** ELF note view wrapping a note header. */                                 \
  using Elf_Note = typename ELFT::Note;                                        \
  /** Forward iterator over ELF notes. */                                      \
  using Elf_Note_Iterator = typename ELFT::NoteIterator;                       \
  /** Call-graph profile section entry. */                                     \
  using Elf_CGProfile = typename ELFT::CGProfile;                              \
  /** Contiguous range of dynamic table entries. */                            \
  using Elf_Dyn_Range = typename ELFT::DynRange;                               \
  /** Contiguous range of section headers. */                                  \
  using Elf_Shdr_Range = typename ELFT::ShdrRange;                             \
  /** Contiguous range of symbol table entries. */                             \
  using Elf_Sym_Range = typename ELFT::SymRange;                               \
  /** Contiguous range of Rel relocations. */                                  \
  using Elf_Rel_Range = typename ELFT::RelRange;                               \
  /** Contiguous range of Rela relocations. */                                 \
  using Elf_Rela_Range = typename ELFT::RelaRange;                             \
  /** Contiguous range of RELR bitmap words. */                                \
  using Elf_Relr_Range = typename ELFT::RelrRange;                             \
  /** Contiguous range of program headers. */                                  \
  using Elf_Phdr_Range = typename ELFT::PhdrRange;

#define LLVM_ELF_COMMA ,
#define LLVM_ELF_IMPORT_TYPES(E, W)                                            \
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFType<E LLVM_ELF_COMMA W>)

/// ELF section header layout (32- or 64-bit).
template <class ELFT> struct Elf_Shdr_Base;

/// 32-bit ELF section header.
template <endianness Endianness>
struct Elf_Shdr_Base<ELFType<Endianness, false>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  Elf_Word sh_name;      ///< Section name (index into string table)
  Elf_Word sh_type;      ///< Section type (SHT_*)
  Elf_Word sh_flags;     ///< Section flags (SHF_*)
  Elf_Addr sh_addr;      ///< Address where section is to be loaded
  Elf_Off sh_offset;     ///< File offset of section data, in bytes
  Elf_Word sh_size;      ///< Size of section, in bytes
  Elf_Word sh_link;      ///< Section type-specific header table index link
  Elf_Word sh_info;      ///< Section type-specific extra information
  Elf_Word sh_addralign; ///< Section address alignment
  Elf_Word sh_entsize;   ///< Size of records contained within the section
};

/// 64-bit ELF section header.
template <endianness Endianness>
struct Elf_Shdr_Base<ELFType<Endianness, true>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  Elf_Word sh_name;       ///< Section name (index into string table)
  Elf_Word sh_type;       ///< Section type (SHT_*)
  Elf_Xword sh_flags;     ///< Section flags (SHF_*)
  Elf_Addr sh_addr;       ///< Address where section is to be loaded
  Elf_Off sh_offset;      ///< File offset of section data, in bytes
  Elf_Xword sh_size;      ///< Size of section, in bytes
  Elf_Word sh_link;       ///< Section type-specific header table index link
  Elf_Word sh_info;       ///< Section type-specific extra information
  Elf_Xword sh_addralign; ///< Section address alignment
  Elf_Xword sh_entsize;   ///< Size of records contained within the section
};

/// ELF section header with helpers on top of \c Elf_Shdr_Base.
template <class ELFT>
struct Elf_Shdr_Impl : Elf_Shdr_Base<ELFT> {
  /// Size of records contained within the section.
  using Elf_Shdr_Base<ELFT>::sh_entsize;
  /// Size of section, in bytes.
  using Elf_Shdr_Base<ELFT>::sh_size;

  /// Get the number of entities this section contains if it has any.
  /// \return Entity count (\c sh_size / \c sh_entsize), or zero if
  ///         \c sh_entsize is zero.
  unsigned getEntityCount() const {
    if (sh_entsize == 0)
      return 0;
    return sh_size / sh_entsize;
  }
};

/// ELF symbol table entry layout (32- or 64-bit).
template <class ELFT> struct Elf_Sym_Base;

/// 32-bit ELF symbol table entry.
template <endianness Endianness>
struct Elf_Sym_Base<ELFType<Endianness, false>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  Elf_Word st_name;       ///< Symbol name (index into string table)
  Elf_Addr st_value;      ///< Value or address associated with the symbol
  Elf_Word st_size;       ///< Size of the symbol
  unsigned char st_info;  ///< Symbol's type and binding attributes
  unsigned char st_other; ///< Must be zero; reserved
  Elf_Half st_shndx;      ///< Which section (header table index) it's defined in
};

/// 64-bit ELF symbol table entry.
template <endianness Endianness>
struct Elf_Sym_Base<ELFType<Endianness, true>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  Elf_Word st_name;       ///< Symbol name (index into string table)
  unsigned char st_info;  ///< Symbol's type and binding attributes
  unsigned char st_other; ///< Must be zero; reserved
  Elf_Half st_shndx;      ///< Which section (header table index) it's defined in
  Elf_Addr st_value;      ///< Value or address associated with the symbol
  Elf_Xword st_size;      ///< Size of the symbol
};

/// ELF symbol table entry with accessors on top of \c Elf_Sym_Base.
template <class ELFT>
struct Elf_Sym_Impl : Elf_Sym_Base<ELFT> {
  /// Symbol type and binding packed into \c st_info.
  using Elf_Sym_Base<ELFT>::st_info;
  /// Section header table index this symbol is defined in.
  using Elf_Sym_Base<ELFT>::st_shndx;
  /// Symbol visibility and other flags stored in \c st_other.
  using Elf_Sym_Base<ELFT>::st_other;
  /// Value or address associated with the symbol.
  using Elf_Sym_Base<ELFT>::st_value;

  // These accessors and mutators correspond to the ELF32_ST_BIND,
  // ELF32_ST_TYPE, and ELF32_ST_INFO macros defined in the ELF specification:
  /// Return the STB_* binding from the high nibble of \c st_info.
  /// \return STB_* binding value.
  unsigned char getBinding() const { return st_info >> 4; }
  /// Return the STT_* type from the low nibble of \c st_info.
  /// \return STT_* type value.
  unsigned char getType() const { return st_info & 0x0f; }
  /// Return the symbol value as a 64-bit integer.
  /// \return Symbol value from \c st_value.
  uint64_t getValue() const { return st_value; }
  /// Set the STB_* binding, preserving the current type.
  /// \param b Binding to store in the high nibble of \c st_info.
  void setBinding(unsigned char b) { setBindingAndType(b, getType()); }
  /// Set the STT_* type, preserving the current binding.
  /// \param t Type to store in the low nibble of \c st_info.
  void setType(unsigned char t) { setBindingAndType(getBinding(), t); }

  /// Pack \p b and \p t into \c st_info.
  /// \param b STB_* binding for the high nibble.
  /// \param t STT_* type for the low nibble.
  void setBindingAndType(unsigned char b, unsigned char t) {
    st_info = (b << 4) + (t & 0x0f);
  }

  /// Access to the STV_xxx flag stored in the first two bits of st_other.
  /// STV_DEFAULT: 0
  /// STV_INTERNAL: 1
  /// STV_HIDDEN: 2
  /// STV_PROTECTED: 3
  /// \return STV_* visibility from the low bits of \c st_other.
  unsigned char getVisibility() const { return st_other & 0x3; }
  /// Set the STV_* visibility stored in the low bits of \c st_other.
  /// \param v Visibility; must be in <tt>0..3</tt>.
  void setVisibility(unsigned char v) {
    assert(v < 4 && "Invalid value for visibility");
    st_other = (st_other & ~0x3) | v;
  }

  /// True when this symbol is defined in the absolute section.
  /// \return True if \c st_shndx is \c SHN_ABS.
  bool isAbsolute() const { return st_shndx == ELF::SHN_ABS; }

  /// True when this is a common (unallocated) symbol.
  /// \return True if the type is \c STT_COMMON or the section is \c SHN_COMMON.
  bool isCommon() const {
    return getType() == ELF::STT_COMMON || st_shndx == ELF::SHN_COMMON;
  }

  /// True when this symbol is defined in some section.
  /// \return True if the symbol is not undefined.
  bool isDefined() const { return !isUndefined(); }

  /// True when \c st_shndx is in the processor-specific reserved range.
  /// \return True if \c st_shndx is between \c SHN_LOPROC and \c SHN_HIPROC.
  bool isProcessorSpecific() const {
    return st_shndx >= ELF::SHN_LOPROC && st_shndx <= ELF::SHN_HIPROC;
  }

  /// True when \c st_shndx is in the OS-specific reserved range.
  /// \return True if \c st_shndx is between \c SHN_LOOS and \c SHN_HIOS.
  bool isOSSpecific() const {
    return st_shndx >= ELF::SHN_LOOS && st_shndx <= ELF::SHN_HIOS;
  }

  /// True when \c st_shndx is a reserved section index.
  /// \return True if \c st_shndx is at least \c SHN_LORESERVE.
  bool isReserved() const {
    // ELF::SHN_HIRESERVE is 0xffff so st_shndx <= ELF::SHN_HIRESERVE is always
    // true and some compilers warn about it.
    return st_shndx >= ELF::SHN_LORESERVE;
  }

  /// True when this symbol is undefined (\c SHN_UNDEF).
  /// \return True if \c st_shndx is \c SHN_UNDEF.
  bool isUndefined() const { return st_shndx == ELF::SHN_UNDEF; }

  /// True when this symbol has non-local binding.
  /// \return True if the binding is not \c STB_LOCAL.
  bool isExternal() const {
    return getBinding() != ELF::STB_LOCAL;
  }

  /// Look up this symbol's name in \p StrTab.
  /// \param StrTab Symbol string table that \c st_name indexes into.
  /// \return Symbol name, or an error if \c st_name is out of range.
  Expected<StringRef> getName(StringRef StrTab) const;
};

template <class ELFT>
Expected<StringRef> Elf_Sym_Impl<ELFT>::getName(StringRef StrTab) const {
  uint32_t Offset = this->st_name;
  if (Offset >= StrTab.size())
    return createStringError(object_error::parse_failed,
                             "st_name (0x%" PRIx32
                             ") is past the end of the string table"
                             " of size 0x%zx",
                             Offset, StrTab.size());
  return StringRef(StrTab.data() + Offset);
}

/// Elf_Versym: This is the structure of entries in the SHT_GNU_versym section
/// (.gnu.version). This structure is identical for ELF32 and ELF64.
template <class ELFT>
struct Elf_Versym_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Half vs_index; ///< Version index with flags (e.g. VERSYM_HIDDEN)
};

/// Elf_Verdef: This is the structure of entries in the SHT_GNU_verdef section
/// (.gnu.version_d). This structure is identical for ELF32 and ELF64.
template <class ELFT>
struct Elf_Verdef_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Half vd_version; ///< Version of this structure (e.g. VER_DEF_CURRENT)
  Elf_Half vd_flags;   ///< Bitwise flags (VER_DEF_*)
  Elf_Half vd_ndx;     ///< Version index, used in .gnu.version entries
  Elf_Half vd_cnt;     ///< Number of Verdaux entries
  Elf_Word vd_hash;    ///< Hash of name
  Elf_Word vd_aux;     ///< Offset to the first Verdaux entry (in bytes)
  Elf_Word vd_next;    ///< Offset to the next Verdef entry (in bytes)

  /// Get the first Verdaux entry for this Verdef.
  /// \return Pointer to the first Verdaux entry at \c vd_aux.
  const Elf_Verdaux *getAux() const {
    return reinterpret_cast<const Elf_Verdaux *>((const char *)this + vd_aux);
  }
};

/// Elf_Verdaux: This is the structure of auxiliary data in the SHT_GNU_verdef
/// section (.gnu.version_d). This structure is identical for ELF32 and ELF64.
template <class ELFT>
struct Elf_Verdaux_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Word vda_name; ///< Version name (offset in string table)
  Elf_Word vda_next; ///< Offset to next Verdaux entry (in bytes)
};

/// Elf_Verneed: This is the structure of entries in the SHT_GNU_verneed
/// section (.gnu.version_r). This structure is identical for ELF32 and ELF64.
template <class ELFT>
struct Elf_Verneed_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Half vn_version; ///< Version of this structure (e.g. VER_NEED_CURRENT)
  Elf_Half vn_cnt;     ///< Number of associated Vernaux entries
  Elf_Word vn_file;    ///< Library name (string table offset)
  Elf_Word vn_aux;     ///< Offset to first Vernaux entry (in bytes)
  Elf_Word vn_next;    ///< Offset to next Verneed entry (in bytes)
};

/// Elf_Vernaux: This is the structure of auxiliary data in SHT_GNU_verneed
/// section (.gnu.version_r). This structure is identical for ELF32 and ELF64.
template <class ELFT>
struct Elf_Vernaux_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Word vna_hash;  ///< Hash of dependency name
  Elf_Half vna_flags; ///< Bitwise Flags (VER_FLAG_*)
  Elf_Half vna_other; ///< Version index, used in .gnu.version entries
  Elf_Word vna_name;  ///< Dependency name
  Elf_Word vna_next;  ///< Offset to next Vernaux entry (in bytes)
};

/// Elf_Dyn_Base: This is the structure of entries in the dynamic
///               table section (.dynamic) look like.
template <class ELFT> struct Elf_Dyn_Base;

/// 32-bit ELF dynamic table entry.
template <endianness Endianness>
struct Elf_Dyn_Base<ELFType<Endianness, false>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  Elf_Sword d_tag; ///< Dynamic table tag (DT_*).
  /// Dynamic-entry payload holding either an integer or an address.
  union {
    Elf_Word d_val; ///< Integer operand of the tag.
    Elf_Addr d_ptr; ///< Virtual address operand of the tag.
  } d_un; ///< Integer or pointer payload selected by \c d_tag.
};

/// 64-bit ELF dynamic table entry.
template <endianness Endianness>
struct Elf_Dyn_Base<ELFType<Endianness, true>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  Elf_Sxword d_tag; ///< Dynamic table tag (DT_*).
  /// Dynamic-entry payload holding either an integer or an address.
  union {
    Elf_Xword d_val; ///< Integer operand of the tag.
    Elf_Addr d_ptr;  ///< Virtual address operand of the tag.
  } d_un; ///< Integer or pointer payload selected by \c d_tag.
};

/// Elf_Dyn_Impl: This inherits from Elf_Dyn_Base, adding getters.
template <class ELFT>
struct Elf_Dyn_Impl : Elf_Dyn_Base<ELFT> {
  /// Dynamic table tag (DT_*).
  using Elf_Dyn_Base<ELFT>::d_tag;
  /// Integer or pointer payload selected by \c d_tag.
  using Elf_Dyn_Base<ELFT>::d_un;
  /// Signed address-width integer.
  using intX_t = std::conditional_t<ELFT::Is64Bits, int64_t, int32_t>;
  /// Unsigned address-width integer.
  using uintX_t = std::conditional_t<ELFT::Is64Bits, uint64_t, uint32_t>;
  /// Return the dynamic table tag.
  /// \return Dynamic table tag (DT_*).
  intX_t getTag() const { return d_tag; }
  /// Return the integer operand of this dynamic entry.
  /// \return Integer payload from \c d_un.d_val.
  uintX_t getVal() const { return d_un.d_val; }
  /// Return the pointer operand of this dynamic entry.
  /// \return Pointer payload from \c d_un.d_ptr.
  uintX_t getPtr() const { return d_un.d_ptr; }
};

/// 32-bit ELF relocation without an addend (\c Elf32_Rel).
template <endianness Endianness>
struct Elf_Rel_Impl<ELFType<Endianness, false>, false> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  static const bool HasAddend = false; ///< Whether this relocation includes an addend.
  static const bool IsCrel = false;    ///< Whether this is a compact CREL relocation.
  Elf_Addr r_offset; ///< Location (file byte offset, or program virtual addr)
  Elf_Word r_info;   ///< Symbol table index and type of relocation to apply

  /// Return \c r_info (MIPS64LE encoding is not used for ELF32).
  /// \param isMips64EL Unused; must be false for ELF32.
  /// \return Encoded symbol index and relocation type from \c r_info.
  uint32_t getRInfo(bool isMips64EL) const {
    assert(!isMips64EL);
    return r_info;
  }
  /// Store \p R into \c r_info.
  /// \param R Encoded symbol index and relocation type.
  /// \param IsMips64EL Unused; must be false for ELF32.
  void setRInfo(uint32_t R, bool IsMips64EL) {
    assert(!IsMips64EL);
    r_info = R;
  }

  // These accessors and mutators correspond to the ELF32_R_SYM, ELF32_R_TYPE,
  // and ELF32_R_INFO macros defined in the ELF specification:
  /// Return the symbol table index from \c r_info.
  /// \param isMips64EL Unused; must be false for ELF32.
  /// \return Symbol table index extracted from the high bits of \c r_info.
  uint32_t getSymbol(bool isMips64EL) const {
    return this->getRInfo(isMips64EL) >> 8;
  }
  /// Return the relocation type from \c r_info.
  /// \param isMips64EL Unused; must be false for ELF32.
  /// \return Relocation type extracted from the low byte of \c r_info.
  unsigned char getType(bool isMips64EL) const {
    return (unsigned char)(this->getRInfo(isMips64EL) & 0x0ff);
  }
  /// Set the symbol table index, preserving the current type.
  /// \param s Symbol table index.
  /// \param IsMips64EL Unused; must be false for ELF32.
  void setSymbol(uint32_t s, bool IsMips64EL) {
    setSymbolAndType(s, getType(IsMips64EL), IsMips64EL);
  }
  /// Set the relocation type, preserving the current symbol index.
  /// \param t Relocation type.
  /// \param IsMips64EL Unused; must be false for ELF32.
  void setType(unsigned char t, bool IsMips64EL) {
    setSymbolAndType(getSymbol(IsMips64EL), t, IsMips64EL);
  }
  /// Pack \p s and \p t into \c r_info.
  /// \param s Symbol table index.
  /// \param t Relocation type.
  /// \param IsMips64EL Unused; must be false for ELF32.
  void setSymbolAndType(uint32_t s, unsigned char t, bool IsMips64EL) {
    this->setRInfo((s << 8) + t, IsMips64EL);
  }
};

/// 32-bit ELF relocation with an addend (\c Elf32_Rela).
template <endianness Endianness>
struct Elf_Rel_Impl<ELFType<Endianness, false>, true>
    : public Elf_Rel_Impl<ELFType<Endianness, false>, false> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  static const bool HasAddend = true; ///< Whether this relocation includes an addend.
  static const bool IsCrel = false;   ///< Whether this is a compact CREL relocation.
  Elf_Sword r_addend; ///< Compute value for relocatable field by adding this
};

/// 64-bit ELF relocation without an addend (\c Elf64_Rel).
template <endianness Endianness>
struct Elf_Rel_Impl<ELFType<Endianness, true>, false> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  static const bool HasAddend = false; ///< Whether this relocation includes an addend.
  static const bool IsCrel = false;    ///< Whether this is a compact CREL relocation.
  Elf_Addr r_offset; ///< Location (file byte offset, or program virtual addr)
  Elf_Xword r_info;  ///< Symbol table index and type of relocation to apply

  /// Decode \c r_info, applying the MIPS64LE encoding when requested.
  ///
  /// Mips64 little endian has a "special" encoding of r_info. Instead of one
  /// 64 bit little endian number, it is a little endian 32 bit number followed
  /// by a 32 bit big endian number.
  /// \param isMips64EL True when decoding a MIPS64 little-endian object.
  /// \return Encoded symbol index and relocation type from \c r_info.
  uint64_t getRInfo(bool isMips64EL) const {
    uint64_t t = r_info;
    if (!isMips64EL)
      return t;
    // Mips64 little endian has a "special" encoding of r_info. Instead of one
    // 64 bit little endian number, it is a little endian 32 bit number followed
    // by a 32 bit big endian number.
    return (t << 32) | ((t >> 8) & 0xff000000) | ((t >> 24) & 0x00ff0000) |
           ((t >> 40) & 0x0000ff00) | ((t >> 56) & 0x000000ff);
  }

  /// Store \p R into \c r_info, applying the MIPS64LE encoding when requested.
  /// \param R Encoded symbol index and relocation type.
  /// \param IsMips64EL True when encoding a MIPS64 little-endian object.
  void setRInfo(uint64_t R, bool IsMips64EL) {
    if (IsMips64EL)
      r_info = (R >> 32) | ((R & 0xff000000) << 8) | ((R & 0x00ff0000) << 24) |
               ((R & 0x0000ff00) << 40) | ((R & 0x000000ff) << 56);
    else
      r_info = R;
  }

  // These accessors and mutators correspond to the ELF64_R_SYM, ELF64_R_TYPE,
  // and ELF64_R_INFO macros defined in the ELF specification:
  /// Return the symbol table index from \c r_info.
  /// \param isMips64EL True when decoding a MIPS64 little-endian object.
  /// \return Symbol table index extracted from the high half of \c r_info.
  uint32_t getSymbol(bool isMips64EL) const {
    return (uint32_t)(this->getRInfo(isMips64EL) >> 32);
  }
  /// Return the relocation type from \c r_info.
  /// \param isMips64EL True when decoding a MIPS64 little-endian object.
  /// \return Relocation type extracted from the low half of \c r_info.
  uint32_t getType(bool isMips64EL) const {
    return (uint32_t)(this->getRInfo(isMips64EL) & 0xffffffffL);
  }
  /// Set the symbol table index, preserving the current type.
  /// \param s Symbol table index.
  /// \param IsMips64EL True when encoding a MIPS64 little-endian object.
  void setSymbol(uint32_t s, bool IsMips64EL) {
    setSymbolAndType(s, getType(IsMips64EL), IsMips64EL);
  }
  /// Set the relocation type, preserving the current symbol index.
  /// \param t Relocation type.
  /// \param IsMips64EL True when encoding a MIPS64 little-endian object.
  void setType(uint32_t t, bool IsMips64EL) {
    setSymbolAndType(getSymbol(IsMips64EL), t, IsMips64EL);
  }
  /// Pack \p s and \p t into \c r_info.
  /// \param s Symbol table index.
  /// \param t Relocation type.
  /// \param IsMips64EL True when encoding a MIPS64 little-endian object.
  void setSymbolAndType(uint32_t s, uint32_t t, bool IsMips64EL) {
    this->setRInfo(((uint64_t)s << 32) + (t & 0xffffffffL), IsMips64EL);
  }
};

/// 64-bit ELF relocation with an addend (\c Elf64_Rela).
template <endianness Endianness>
struct Elf_Rel_Impl<ELFType<Endianness, true>, true>
    : public Elf_Rel_Impl<ELFType<Endianness, true>, false> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  static const bool HasAddend = true; ///< Whether this relocation includes an addend.
  static const bool IsCrel = false;   ///< Whether this is a compact CREL relocation.
  Elf_Sxword r_addend; ///< Compute value for relocatable field by adding this.
};

/// In-memory compact relocation (CREL). The serialized form uses LEB128.
template <bool Is64> struct Elf_Crel_Impl {
  /// Unsigned address-width integer (32 or 64 bits).
  using uint = std::conditional_t<Is64, uint64_t, uint32_t>;
  static const bool HasAddend = true; ///< Whether this relocation includes an addend.
  static const bool IsCrel = true;    ///< Whether this is a compact CREL relocation.
  uint r_offset;     ///< Location (file byte offset, or program virtual addr)
  uint32_t r_symidx; ///< Symbol table index
  uint32_t r_type;   ///< Relocation type
  std::conditional_t<Is64, int64_t, int32_t> r_addend; ///< Relocation addend

  // Dummy bool parameter is for compatibility with Elf_Rel_Impl.
  /// Return the relocation type.
  /// \param IsMips64EL Unused; present for compatibility with \c Elf_Rel_Impl.
  /// \return Relocation type from \c r_type.
  uint32_t getType(bool IsMips64EL) const {
    (void)IsMips64EL;
    return r_type;
  }
  /// Return the symbol table index.
  /// \param IsMips64EL Unused; present for compatibility with \c Elf_Rel_Impl.
  /// \return Symbol table index from \c r_symidx.
  uint32_t getSymbol(bool IsMips64EL) const {
    (void)IsMips64EL;
    return r_symidx;
  }
  /// Set the symbol index and relocation type.
  /// \param s Symbol table index.
  /// \param t Relocation type.
  /// \param IsMips64EL Unused; present for compatibility with \c Elf_Rel_Impl.
  void setSymbolAndType(uint32_t s, unsigned char t, bool IsMips64EL) {
    (void)IsMips64EL;
    r_symidx = s;
    r_type = t;
  }
};

/// ELF file header (\c Elf32_Ehdr / \c Elf64_Ehdr).
template <class ELFT>
struct Elf_Ehdr_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  unsigned char e_ident[ELF::EI_NIDENT]; ///< ELF Identification bytes
  Elf_Half e_type;                       ///< Type of file (see ET_*)
  Elf_Half e_machine;   ///< Required architecture for this file (see EM_*)
  Elf_Word e_version;   ///< Must be equal to 1
  Elf_Addr e_entry;     ///< Address to jump to in order to start program
  Elf_Off e_phoff;      ///< Program header table's file offset, in bytes
  Elf_Off e_shoff;      ///< Section header table's file offset, in bytes
  Elf_Word e_flags;     ///< Processor-specific flags
  Elf_Half e_ehsize;    ///< Size of ELF header, in bytes
  Elf_Half e_phentsize; ///< Size of an entry in the program header table
  Elf_Half e_phnum;     ///< Number of entries in the program header table
  Elf_Half e_shentsize; ///< Size of an entry in the section header table
  Elf_Half e_shnum;     ///< Number of entries in the section header table
  /// Section header table index of the section-name string table.
  Elf_Half e_shstrndx;

  /// True when \c e_ident starts with the ELF magic number.
  /// \return True if the identification bytes begin with the ELF magic.
  bool checkMagic() const {
    return (memcmp(e_ident, ELF::ElfMagic, strlen(ELF::ElfMagic))) == 0;
  }

  /// Return the ELF file class from \c e_ident[EI_CLASS].
  /// \return ELFCLASS32 or ELFCLASS64 identification value.
  unsigned char getFileClass() const { return e_ident[ELF::EI_CLASS]; }
  /// Return the data encoding from \c e_ident[EI_DATA].
  /// \return ELFDATA2LSB or ELFDATA2MSB identification value.
  unsigned char getDataEncoding() const { return e_ident[ELF::EI_DATA]; }
};

/// 32-bit ELF program header (\c Elf32_Phdr).
template <endianness Endianness>
struct Elf_Phdr_Impl<ELFType<Endianness, false>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  Elf_Word p_type;   ///< Type of segment
  Elf_Off p_offset;  ///< FileOffset where segment is located, in bytes
  Elf_Addr p_vaddr;  ///< Virtual Address of beginning of segment
  Elf_Addr p_paddr;  ///< Physical address of beginning of segment (OS-specific)
  Elf_Word p_filesz; ///< Num. of bytes in file image of segment (may be zero)
  Elf_Word p_memsz;  ///< Num. of bytes in mem image of segment (may be zero)
  Elf_Word p_flags;  ///< Segment flags
  Elf_Word p_align;  ///< Segment alignment constraint
};

/// 64-bit ELF program header (\c Elf64_Phdr).
template <endianness Endianness>
struct Elf_Phdr_Impl<ELFType<Endianness, true>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  Elf_Word p_type;    ///< Type of segment
  Elf_Word p_flags;   ///< Segment flags
  Elf_Off p_offset;   ///< FileOffset where segment is located, in bytes
  Elf_Addr p_vaddr;   ///< Virtual Address of beginning of segment
  Elf_Addr p_paddr;   ///< Physical address of beginning of segment (OS-specific)
  Elf_Xword p_filesz; ///< Num. of bytes in file image of segment (may be zero)
  Elf_Xword p_memsz;  ///< Num. of bytes in mem image of segment (may be zero)
  Elf_Xword p_align;  ///< Segment alignment constraint
};

/// SysV \c .hash section header and bucket/chain accessors.
template <class ELFT>
struct Elf_Hash_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Word nbucket; ///< Number of hash buckets
  Elf_Word nchain;  ///< Number of chain entries (usually the symbol count)

  /// Return the hash bucket array that follows this header.
  /// \return Hash buckets of length \c nbucket.
  ArrayRef<Elf_Word> buckets() const {
    return ArrayRef<Elf_Word>(&nbucket + 2, &nbucket + 2 + nbucket);
  }

  /// Return the hash chain array that follows the buckets.
  /// \return Hash chains of length \c nchain.
  ArrayRef<Elf_Word> chains() const {
    return ArrayRef<Elf_Word>(&nbucket + 2 + nbucket,
                              &nbucket + 2 + nbucket + nchain);
  }
};

/// GNU \c .gnu.hash section header and Bloom-filter/bucket accessors.
template <class ELFT>
struct Elf_GnuHash_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Word nbuckets;  ///< Number of hash buckets
  Elf_Word symndx;    ///< Index of the first symbol in the GNU hash table
  Elf_Word maskwords; ///< Number of Bloom-filter words
  Elf_Word shift2;    ///< Bloom-filter shift count

  /// Return the Bloom-filter bitmask that follows this header.
  /// \return Bloom-filter words of length \c maskwords.
  ArrayRef<Elf_Off> filter() const {
    return ArrayRef<Elf_Off>(reinterpret_cast<const Elf_Off *>(&shift2 + 1),
                             maskwords);
  }

  /// Return the hash bucket array that follows the Bloom filter.
  /// \return Hash buckets of length \c nbuckets.
  ArrayRef<Elf_Word> buckets() const {
    return ArrayRef<Elf_Word>(
        reinterpret_cast<const Elf_Word *>(filter().end()), nbuckets);
  }

  /// Return GNU hash values for dynamic symbols starting at \c symndx.
  /// \param DynamicSymCount Number of symbols in the dynamic symbol table.
  /// \return Hash values for symbols from \c symndx through the last dynamic
  ///         symbol.
  ArrayRef<Elf_Word> values(unsigned DynamicSymCount) const {
    assert(DynamicSymCount >= symndx);
    return ArrayRef<Elf_Word>(buckets().end(), DynamicSymCount - symndx);
  }
};

// http://www.sco.com/developers/gabi/latest/ch4.sheader.html#compression_header
/// 32-bit ELF compression header (\c Elf32_Chdr).
template <endianness Endianness>
struct Elf_Chdr_Impl<ELFType<Endianness, false>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  Elf_Word ch_type;      ///< Compression format (ELFCOMPRESS_*)
  Elf_Word ch_size;      ///< Uncompressed data size in bytes
  Elf_Word ch_addralign; ///< Uncompressed data alignment
};

/// 64-bit ELF compression header (\c Elf64_Chdr).
template <endianness Endianness>
struct Elf_Chdr_Impl<ELFType<Endianness, true>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  Elf_Word ch_type;       ///< Compression format (ELFCOMPRESS_*)
  Elf_Word ch_reserved;   ///< Reserved; must be zero
  Elf_Xword ch_size;      ///< Uncompressed data size in bytes
  Elf_Xword ch_addralign; ///< Uncompressed data alignment
};

/// Note header
template <class ELFT>
struct Elf_Nhdr_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Word n_namesz; ///< Size of the note name including the terminating NUL
  Elf_Word n_descsz; ///< Size of the note descriptor in bytes
  Elf_Word n_type;   ///< Note type (owner-specific)

  /// Return this note's on-disk size including padded name and descriptor.
  ///
  /// Both the start and the end of the descriptor are aligned by the section
  /// alignment. In practice many 64-bit systems deviate from the generic ABI by
  /// using sh_addralign=4.
  /// \param Align Alignment applied to the name and descriptor, typically the
  ///        note section's \c sh_addralign.
  /// \return Total byte size of the note header, name, and descriptor.
  size_t getSize(size_t Align) const {
    return alignToPowerOf2(sizeof(*this) + n_namesz, Align) +
           alignToPowerOf2(n_descsz, Align);
  }
};

/// An ELF note.
///
/// Wraps a note header, providing methods for accessing the name and
/// descriptor safely.
template <class ELFT>
class Elf_Note_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

  const Elf_Nhdr_Impl<ELFT> &Nhdr;

  template <class NoteIteratorELFT> friend class Elf_Note_Iterator_Impl;

public:
  /// Construct a note view from its header.
  /// \param Nhdr Note header that prefixes the name and descriptor bytes.
  Elf_Note_Impl(const Elf_Nhdr_Impl<ELFT> &Nhdr) : Nhdr(Nhdr) {}

  /// Get the note's name, excluding the terminating null byte.
  /// \return Note owner name, or empty if \c n_namesz is zero.
  StringRef getName() const {
    if (!Nhdr.n_namesz)
      return StringRef();
    return StringRef(reinterpret_cast<const char *>(&Nhdr) + sizeof(Nhdr),
                     Nhdr.n_namesz - 1);
  }

  /// Get the note's descriptor.
  /// \param Align Alignment of the name and descriptor, typically
  ///        \c sh_addralign.
  /// \return Descriptor bytes, or empty if \c n_descsz is zero.
  ArrayRef<uint8_t> getDesc(size_t Align) const {
    if (!Nhdr.n_descsz)
      return ArrayRef<uint8_t>();
    return ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(&Nhdr) +
            alignToPowerOf2(sizeof(Nhdr) + Nhdr.n_namesz, Align),
        Nhdr.n_descsz);
  }

  /// Get the note's descriptor as StringRef
  /// \param Align Alignment of the name and descriptor, typically
  ///        \c sh_addralign.
  /// \return Descriptor contents interpreted as a byte string.
  StringRef getDescAsStringRef(size_t Align) const {
    ArrayRef<uint8_t> Desc = getDesc(Align);
    return StringRef(reinterpret_cast<const char *>(Desc.data()), Desc.size());
  }

  /// Get the note's type.
  /// \return Owner-specific note type from \c n_type.
  Elf_Word getType() const { return Nhdr.n_type; }
};

/// Forward iterator over ELF notes in a section or segment.
template <class ELFT> class Elf_Note_Iterator_Impl {
public:
  /// Iterator category; notes are visited sequentially.
  using iterator_category = std::forward_iterator_tag;
  /// Note view yielded by this iterator.
  using value_type = Elf_Note_Impl<ELFT>;
  /// Distance type between note iterators.
  using difference_type = std::ptrdiff_t;
  /// Pointer to a note view (unused; this iterator is not a real pointer).
  using pointer = value_type *;
  /// Reference to a note view (unused; \c operator* returns by value).
  using reference = value_type &;

private:
  // Nhdr being a nullptr marks the end of iteration.
  const Elf_Nhdr_Impl<ELFT> *Nhdr = nullptr;
  size_t RemainingSize = 0u;
  size_t Align = 0;
  Error *Err = nullptr;

  template <class ELFFileELFT> friend class ELFFile;

  // Stop iteration and indicate an overflow.
  void stopWithOverflowError() {
    Nhdr = nullptr;
    *Err = make_error<StringError>("ELF note overflows container",
                                   object_error::parse_failed);
  }

  // Advance Nhdr by NoteSize bytes, starting from NhdrPos.
  //
  // Assumes NoteSize <= RemainingSize. Ensures Nhdr->getSize() <= RemainingSize
  // upon returning. Handles stopping iteration when reaching the end of the
  // container, either cleanly or with an overflow error.
  void advanceNhdr(const uint8_t *NhdrPos, size_t NoteSize) {
    RemainingSize -= NoteSize;
    if (RemainingSize == 0u) {
      // Ensure that if the iterator walks to the end, the error is checked
      // afterwards.
      *Err = Error::success();
      Nhdr = nullptr;
    } else if (sizeof(*Nhdr) > RemainingSize)
      stopWithOverflowError();
    else {
      Nhdr = reinterpret_cast<const Elf_Nhdr_Impl<ELFT> *>(NhdrPos + NoteSize);
      if (Nhdr->getSize(Align) > RemainingSize)
        stopWithOverflowError();
      else
        *Err = Error::success();
    }
  }

  Elf_Note_Iterator_Impl() = default;
  explicit Elf_Note_Iterator_Impl(Error &Err) : Err(&Err) {}
  Elf_Note_Iterator_Impl(const uint8_t *Start, size_t Size, size_t Align,
                         Error &Err)
      : RemainingSize(Size), Align(Align), Err(&Err) {
    consumeError(std::move(Err));
    assert(Start && "ELF note iterator starting at NULL");
    advanceNhdr(Start, 0u);
  }

public:
  /// Advance to the next note in the container.
  /// \return Reference to this iterator after advancing.
  Elf_Note_Iterator_Impl &operator++() {
    assert(Nhdr && "incremented ELF note end iterator");
    const uint8_t *NhdrPos = reinterpret_cast<const uint8_t *>(Nhdr);
    size_t NoteSize = Nhdr->getSize(Align);
    advanceNhdr(NhdrPos, NoteSize);
    return *this;
  }
  /// Compare two note iterators for equality.
  /// \param Other Iterator to compare with.
  /// \return True if both iterators refer to the same note header.
  bool operator==(Elf_Note_Iterator_Impl Other) const {
    if (!Nhdr && Other.Err)
      (void)(bool)(*Other.Err);
    if (!Other.Nhdr && Err)
      (void)(bool)(*Err);
    return Nhdr == Other.Nhdr;
  }
  /// Compare two note iterators for inequality.
  /// \param Other Iterator to compare with.
  /// \return True if the iterators refer to different note headers.
  bool operator!=(Elf_Note_Iterator_Impl Other) const {
    return !(*this == Other);
  }
  /// Return the note at the current position.
  /// \return Note view for the current header.
  Elf_Note_Impl<ELFT> operator*() const {
    assert(Nhdr && "dereferenced ELF note end iterator");
    return Elf_Note_Impl<ELFT>(*Nhdr);
  }
};

/// Call-graph profile section entry (\c .llvm.call-graph-profile).
template <class ELFT> struct Elf_CGProfile_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Xword cgp_weight; ///< Weight of this call-graph edge
};

/// MIPS \c .reginfo section entry.
template <class ELFT>
struct Elf_Mips_RegInfo;

/// 32-bit MIPS \c .reginfo section entry.
template <llvm::endianness Endianness>
struct Elf_Mips_RegInfo<ELFType<Endianness, false>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  Elf_Word ri_gprmask;    ///< bit-mask of used general registers
  Elf_Word ri_cprmask[4]; ///< bit-mask of used co-processor registers
  Elf_Addr ri_gp_value;   ///< gp register value
};

/// 64-bit MIPS \c .reginfo section entry.
template <llvm::endianness Endianness>
struct Elf_Mips_RegInfo<ELFType<Endianness, true>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  Elf_Word ri_gprmask;    ///< bit-mask of used general registers
  Elf_Word ri_pad;        ///< unused padding field
  Elf_Word ri_cprmask[4]; ///< bit-mask of used co-processor registers
  Elf_Addr ri_gp_value;   ///< gp register value
};

/// MIPS \c .MIPS.options section descriptor header.
template <class ELFT> struct Elf_Mips_Options {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  uint8_t kind;     ///< Determines interpretation of variable part of descriptor
  uint8_t size;     ///< Byte size of descriptor, including this header
  Elf_Half section; ///< Section header index of section affected,
                    ///< or 0 for global options
  Elf_Word info;    ///< Kind-specific information

  /// Return the MIPS register-info payload that follows this options header.
  /// \return Mutable reference to the ODK_REGINFO register-info payload.
  Elf_Mips_RegInfo<ELFT> &getRegInfo() {
    assert(kind == ELF::ODK_REGINFO);
    return *reinterpret_cast<Elf_Mips_RegInfo<ELFT> *>(
        (uint8_t *)this + sizeof(Elf_Mips_Options));
  }
  /// Return the MIPS register-info payload that follows this options header.
  /// \return Const reference to the ODK_REGINFO register-info payload.
  const Elf_Mips_RegInfo<ELFT> &getRegInfo() const {
    return const_cast<Elf_Mips_Options *>(this)->getRegInfo();
  }
};

/// MIPS \c .MIPS.abiflags section contents.
template <class ELFT> struct Elf_Mips_ABIFlags {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Half version;  ///< Version of the structure
  uint8_t isa_level; ///< ISA level: 1-5, 32, and 64
  uint8_t isa_rev;   ///< ISA revision (0 for MIPS I - MIPS V)
  uint8_t gpr_size;  ///< General purpose registers size
  uint8_t cpr1_size; ///< Co-processor 1 registers size
  uint8_t cpr2_size; ///< Co-processor 2 registers size
  uint8_t fp_abi;    ///< Floating-point ABI flag
  Elf_Word isa_ext;  ///< Processor-specific extension
  Elf_Word ases;     ///< ASEs flags
  Elf_Word flags1;   ///< General flags
  Elf_Word flags2;   ///< General flags
};

} // end namespace object.
} // end namespace llvm.

#endif // LLVM_OBJECT_ELFTYPES_H
