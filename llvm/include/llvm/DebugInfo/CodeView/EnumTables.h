//===- EnumTables.h - Enum to string conversion tables ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_ENUMTABLES_H
#define LLVM_DEBUGINFO_CODEVIEW_ENUMTABLES_H

#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {
template <typename, unsigned> class EnumStrings;
namespace codeview {

/// Return the enumerator name table for \c SymbolKind.
///
/// \returns The enumerator name table for \c SymbolKind.
LLVM_ABI EnumStrings<SymbolKind, 1> getSymbolTypeNames();
/// Return the enumerator name table for \c TypeLeafKind.
///
/// \returns The enumerator name table for \c TypeLeafKind.
LLVM_ABI EnumStrings<TypeLeafKind, 1> getTypeLeafNames();
/// Return the enumerator name table for \c RegisterId for the given CPU.
///
/// \param Cpu Target CPU whose register name set should be returned.
/// \returns The enumerator name table for \c RegisterId for the given CPU.
LLVM_ABI EnumStrings<uint16_t, 1> getRegisterNames(CPUType Cpu);
/// Return the enumerator name table for \c PublicSymFlags.
///
/// \returns The enumerator name table for \c PublicSymFlags.
LLVM_ABI EnumStrings<uint32_t, 1> getPublicSymFlagNames();
/// Return the enumerator name table for \c ProcSymFlags.
///
/// \returns The enumerator name table for \c ProcSymFlags.
LLVM_ABI EnumStrings<uint8_t, 1> getProcSymFlagNames();
/// Return the enumerator name table for \c LocalSymFlags.
///
/// \returns The enumerator name table for \c LocalSymFlags.
LLVM_ABI EnumStrings<uint16_t, 1> getLocalFlagNames();
/// Return the enumerator name table for \c FrameCookieKind.
///
/// \returns The enumerator name table for \c FrameCookieKind.
LLVM_ABI EnumStrings<uint8_t, 1> getFrameCookieKindNames();
/// Return the enumerator name table for \c SourceLanguage.
///
/// \returns The enumerator name table for \c SourceLanguage.
LLVM_ABI EnumStrings<SourceLanguage, 1> getSourceLanguageNames();
/// Return the enumerator name table for \c CompileSym2Flags.
///
/// \returns The enumerator name table for \c CompileSym2Flags.
LLVM_ABI EnumStrings<uint32_t, 1> getCompileSym2FlagNames();
/// Return the enumerator name table for \c CompileSym3Flags.
///
/// \returns The enumerator name table for \c CompileSym3Flags.
LLVM_ABI EnumStrings<uint32_t, 1> getCompileSym3FlagNames();
/// Return the enumerator name table for \c FileChecksumKind.
///
/// \returns The enumerator name table for \c FileChecksumKind.
LLVM_ABI EnumStrings<uint32_t, 1> getFileChecksumNames();
/// Return the enumerator name table for \c CPUType.
///
/// \returns The enumerator name table for \c CPUType.
LLVM_ABI EnumStrings<unsigned, 1> getCPUTypeNames();
/// Return the enumerator name table for \c FrameProcedureOptions.
///
/// \returns The enumerator name table for \c FrameProcedureOptions.
LLVM_ABI EnumStrings<uint32_t, 1> getFrameProcSymFlagNames();
/// Return the enumerator name table for \c ExportFlags.
///
/// \returns The enumerator name table for \c ExportFlags.
LLVM_ABI EnumStrings<uint16_t, 1> getExportSymFlagNames();
/// Return the enumerator name table for \c DebugSubsectionKind.
///
/// \returns The enumerator name table for \c DebugSubsectionKind.
LLVM_ABI EnumStrings<uint32_t, 1> getModuleSubstreamKindNames();
/// Return the enumerator name table for \c ThunkOrdinal.
///
/// \returns The enumerator name table for \c ThunkOrdinal.
LLVM_ABI EnumStrings<uint8_t, 1> getThunkOrdinalNames();
/// Return the enumerator name table for \c TrampolineType.
///
/// \returns The enumerator name table for \c TrampolineType.
LLVM_ABI EnumStrings<uint16_t, 1> getTrampolineNames();
/// Return the enumerator name table for \c COFF::SectionCharacteristics.
///
/// \returns The enumerator name table for \c COFF::SectionCharacteristics.
LLVM_ABI EnumStrings<COFF::SectionCharacteristics, 1>
getImageSectionCharacteristicNames();
/// Return the enumerator name table for \c ClassOptions.
///
/// \returns The enumerator name table for \c ClassOptions.
LLVM_ABI EnumStrings<uint16_t, 1> getClassOptionNames();
/// Return the enumerator name table for \c MemberAccess.
///
/// \returns The enumerator name table for \c MemberAccess.
LLVM_ABI EnumStrings<uint8_t, 1> getMemberAccessNames();
/// Return the enumerator name table for \c MethodOptions.
///
/// \returns The enumerator name table for \c MethodOptions.
LLVM_ABI EnumStrings<uint16_t, 1> getMethodOptionNames();
/// Return the enumerator name table for \c MethodKind.
///
/// \returns The enumerator name table for \c MethodKind.
LLVM_ABI EnumStrings<uint16_t, 1> getMemberKindNames();
/// Return the enumerator name table for \c PointerKind.
///
/// \returns The enumerator name table for \c PointerKind.
LLVM_ABI EnumStrings<uint8_t, 1> getPtrKindNames();
/// Return the enumerator name table for \c PointerMode.
///
/// \returns The enumerator name table for \c PointerMode.
LLVM_ABI EnumStrings<uint8_t, 1> getPtrModeNames();
/// Return the enumerator name table for \c PointerToMemberRepresentation.
///
/// \returns The enumerator name table for \c PointerToMemberRepresentation.
LLVM_ABI EnumStrings<uint16_t, 1> getPtrMemberRepNames();
/// Return the enumerator name table for \c ModifierOptions.
///
/// \returns The enumerator name table for \c ModifierOptions.
LLVM_ABI EnumStrings<uint16_t, 1> getTypeModifierNames();
/// Return the enumerator name table for \c CallingConvention.
///
/// \returns The enumerator name table for \c CallingConvention.
LLVM_ABI EnumStrings<uint8_t, 1> getCallingConventions();
/// Return the enumerator name table for \c FunctionOptions.
///
/// \returns The enumerator name table for \c FunctionOptions.
LLVM_ABI EnumStrings<uint8_t, 1> getFunctionOptionEnum();
/// Return the enumerator name table for \c LabelType.
///
/// \returns The enumerator name table for \c LabelType.
LLVM_ABI EnumStrings<uint16_t, 1> getLabelTypeEnum();
/// Return the enumerator name table for \c JumpTableEntrySize.
///
/// \returns The enumerator name table for \c JumpTableEntrySize.
LLVM_ABI EnumStrings<uint16_t, 1> getJumpTableEntrySizeNames();

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_ENUMTABLES_H
