//===--- DWARFEmitter.h - ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Common declarations for yaml2obj
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_DWARFEMITTER_H
#define LLVM_OBJECTYAML_DWARFEMITTER_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SwapByteOrder.h"
#include "llvm/TargetParser/Host.h"
#include <memory>

namespace llvm {

class raw_ostream;

/// Emitters that serialize DWARFYAML data into binary DWARF debug sections.
namespace DWARFYAML {

struct Data;

/// Emit the .debug_abbrev section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugAbbrev(raw_ostream &OS, const Data &DI);
/// Emit the .debug_str section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugStr(raw_ostream &OS, const Data &DI);

/// Emit the .debug_aranges section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugAranges(raw_ostream &OS, const Data &DI);
/// Emit the .debug_ranges section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugRanges(raw_ostream &OS, const Data &DI);
/// Emit the .debug_pubnames section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugPubnames(raw_ostream &OS, const Data &DI);
/// Emit the .debug_pubtypes section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugPubtypes(raw_ostream &OS, const Data &DI);
/// Emit the .debug_gnu_pubnames section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugGNUPubnames(raw_ostream &OS, const Data &DI);
/// Emit the .debug_gnu_pubtypes section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugGNUPubtypes(raw_ostream &OS, const Data &DI);
/// Emit the .debug_info section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugInfo(raw_ostream &OS, const Data &DI);
/// Emit the .debug_line section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugLine(raw_ostream &OS, const Data &DI);
/// Emit the .debug_addr section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugAddr(raw_ostream &OS, const Data &DI);
/// Emit the .debug_str_offsets section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugStrOffsets(raw_ostream &OS, const Data &DI);
/// Emit the .debug_rnglists section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugRnglists(raw_ostream &OS, const Data &DI);
/// Emit the .debug_loclists section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugLoclists(raw_ostream &OS, const Data &DI);
/// Emit the .debug_names section described by \p DI to \p OS.
/// \param OS Output stream that receives the binary section contents.
/// \param DI YAML DWARF data that provides the section to emit.
/// \return Success, or an error if emission fails.
LLVM_ABI Error emitDebugNames(raw_ostream &OS, const Data &DI);

/// Return the emitter for the DWARF section named \p SecName.
/// \param SecName Section name without a leading dot (e.g. "debug_info").
/// \return A function that emits that section, or one that reports unsupported.
LLVM_ABI std::function<Error(raw_ostream &, const Data &)>
getDWARFEmitterByName(StringRef SecName);
/// Parse \p YAMLString and emit all non-empty DWARF sections it describes.
/// \param YAMLString YAML text describing DWARF debug sections.
/// \param IsLittleEndian Whether to emit little-endian DWARF.
/// \param Is64BitAddrSize Whether target addresses are 64-bit.
/// \return A map from section name to buffer, or an error on failure.
LLVM_ABI Expected<StringMap<std::unique_ptr<MemoryBuffer>>>
emitDebugSections(StringRef YAMLString,
                  bool IsLittleEndian = sys::IsLittleEndianHost,
                  bool Is64BitAddrSize = true);
} // end namespace DWARFYAML
} // end namespace llvm

#endif // LLVM_OBJECTYAML_DWARFEMITTER_H
