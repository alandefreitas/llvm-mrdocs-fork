//===- llvm/DWP/ELFWriter.h - ELF structure writer -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared utilities for writing ELF header and section header structures.
// Used by both the MC ELFObjectWriter and the DWP direct ELF writer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWP_ELFWRITER_H
#define LLVM_DWP_ELFWRITER_H

#include "llvm/Support/EndianStream.h"
#include <cstdint>

namespace llvm {
namespace ELF {

/// Write an ELF file header (Elf32_Ehdr or Elf64_Ehdr) for an ET_REL object.
///
/// \param W Output writer that receives the header bytes.
/// \param Is64Bit When true, write an Elf64_Ehdr; otherwise Elf32_Ehdr.
/// \param OSABI OS/ABI identification written to e_ident[EI_OSABI].
/// \param ABIVersion ABI version written to e_ident[EI_ABIVERSION].
/// \param EMachine Target architecture written to e_machine.
/// \param EFlags Processor-specific flags written to e_flags.
/// \param SHOff File offset of the section header table (e_shoff).
/// \param SHNum Number of section header entries (e_shnum).
/// \param SHStrNdx Section header string table index (e_shstrndx).
LLVM_ABI void writeHeader(support::endian::Writer &W, bool Is64Bit,
                          uint8_t OSABI, uint8_t ABIVersion, uint16_t EMachine,
                          uint32_t EFlags, uint64_t SHOff, uint16_t SHNum,
                          uint16_t SHStrNdx);

/// Write a single ELF section header entry (Elf32_Shdr or Elf64_Shdr).
///
/// \param W Output writer that receives the section header bytes.
/// \param Is64Bit When true, write an Elf64_Shdr; otherwise Elf32_Shdr.
/// \param Name Offset of the section name in the section header string table.
/// \param Type Section type (sh_type).
/// \param Flags Section flags (sh_flags).
/// \param Address Virtual address of the section in memory (sh_addr).
/// \param Offset File offset of the section data (sh_offset).
/// \param Size Size of the section in bytes (sh_size).
/// \param Link Section header table index link (sh_link).
/// \param Info Extra information, interpretation depends on Type (sh_info).
/// \param Alignment Required section alignment (sh_addralign).
/// \param EntrySize Size of each entry for fixed-size table sections
///        (sh_entsize).
LLVM_ABI void writeSectionHeader(support::endian::Writer &W, bool Is64Bit,
                                 uint32_t Name, uint32_t Type, uint64_t Flags,
                                 uint64_t Address, uint64_t Offset,
                                 uint64_t Size, uint32_t Link, uint32_t Info,
                                 uint64_t Alignment, uint64_t EntrySize);

} // namespace ELF
} // namespace llvm

#endif // LLVM_DWP_ELFWRITER_H
