//===------------- MachO.h - MachO format utilities -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains utilities for load MachO relocatable object files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MACHO_H
#define LLVM_EXECUTIONENGINE_ORC_MACHO_H

#include "llvm/ExecutionEngine/Orc/CoreContainers.h"
#include "llvm/ExecutionEngine/Orc/LoadLinkableFile.h"
#include "llvm/Object/Archive.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

namespace object {

class Archive;
class MachOUniversalBinary;

} // namespace object

namespace orc {

class ExecutionSession;
class JITDylib;
class ObjectLayer;

/// Check that a buffer contains a MachO object compatible with a triple.
///
/// ObjIsSlice should be set to true if Obj is a slice of a universal binary
/// (that fact will then be reported in the error messages).
/// @param Obj Buffer expected to contain a MachO relocatable object.
/// @param TT Triple that the object must be compatible with.
/// @param ObjIsSlice Whether \p Obj is a slice of a universal binary.
/// @return Success, or an error if \p Obj is not a compatible MachO
///         relocatable object.
LLVM_ABI Error checkMachORelocatableObject(MemoryBufferRef Obj,
                                           const Triple &TT, bool ObjIsSlice);

/// Check that a buffer contains a MachO object compatible with a triple.
///
/// This convenience overload returns the buffer if it passes all checks,
/// otherwise it returns an error.
/// @param Obj Buffer expected to contain a MachO relocatable object.
/// @param TT Triple that the object must be compatible with.
/// @param ObjIsSlice Whether \p Obj is a slice of a universal binary.
/// @return The validated buffer, or an error if checks fail.
LLVM_ABI Expected<std::unique_ptr<MemoryBuffer>>
checkMachORelocatableObject(std::unique_ptr<MemoryBuffer> Obj, const Triple &TT,
                            bool ObjIsSlice);

/// Load a relocatable object compatible with TT from Path.
///
/// If Path is a universal binary, this function will return a buffer for the
/// slice compatible with Triple (if one is present).
/// @param Path Path to the relocatable object or universal binary.
/// @param TT Triple that the loaded object must be compatible with.
/// @param LA Whether archives are never, optionally, or required to be loaded.
/// @param IdentifierOverride Optional name to use for the resulting buffer
///        instead of \p Path.
/// @return Buffer and linkable-file kind for the loaded object, or an error.
LLVM_ABI Expected<std::pair<std::unique_ptr<MemoryBuffer>, LinkableFileKind>>
loadMachOLinkableFile(
    StringRef Path, const Triple &TT, LoadArchives LA,
    std::optional<StringRef> IdentifierOverride = std::nullopt);

/// Load a compatible relocatable object from a MachO universal binary.
///
/// Path is only used for error reporting. Identifier will be used to name the
/// resulting buffer.
/// @param FD File descriptor for the universal binary.
/// @param UBBuf Buffer covering the universal binary contents.
/// @param TT Triple used to select a compatible slice.
/// @param LA Whether archives are never, optionally, or required to be loaded.
/// @param UBPath Path used only for error reporting.
/// @param Identifier Name to assign to the resulting buffer.
/// @return Buffer and linkable-file kind for the selected slice, or an error.
LLVM_ABI Expected<std::pair<std::unique_ptr<MemoryBuffer>, LinkableFileKind>>
loadLinkableSliceFromMachOUniversalBinary(sys::fs::file_t FD,
                                          std::unique_ptr<MemoryBuffer> UBBuf,
                                          const Triple &TT, LoadArchives LA,
                                          StringRef UBPath,
                                          StringRef Identifier);

/// Utility for identifying the file-slice compatible with TT in a universal
/// binary.
/// @param UB Universal binary to search for a compatible slice.
/// @param TT Triple used to select the slice.
/// @return Offset and size of the compatible slice, or an error if none
///         matches.
LLVM_ABI Expected<std::pair<size_t, size_t>>
getMachOSliceRangeForTriple(object::MachOUniversalBinary &UB, const Triple &TT);

/// Utility for identifying the file-slice compatible with TT in a universal
/// binary.
/// @param UBBuf Buffer containing a MachO universal binary.
/// @param TT Triple used to select the slice.
/// @return Offset and size of the compatible slice, or an error if none
///         matches.
LLVM_ABI Expected<std::pair<size_t, size_t>>
getMachOSliceRangeForTriple(MemoryBufferRef UBBuf, const Triple &TT);

/// For use with StaticLibraryDefinitionGenerators.
class ForceLoadMachOArchiveMembers {
public:
  /// Construct a visitor that force-loads MachO archive members.
  /// @param L Object layer used to add loaded members.
  /// @param JD JITDylib that loaded members are added to.
  /// @param ObjCOnly If true, only load members with Objective-C/Swift
  ///        metadata; otherwise load every member.
  ForceLoadMachOArchiveMembers(ObjectLayer &L, JITDylib &JD, bool ObjCOnly)
      : L(L), JD(JD), ObjCOnly(ObjCOnly) {}

  /// Visit an archive member and force-load it when required.
  ///
  /// Returns false if the member was loaded (or is not loadable), true if it
  /// remains available for normal loading, or an Error if the archive is
  /// invalid.
  /// @param A Archive containing the member.
  /// @param MemberBuf Buffer covering the archive member contents.
  /// @param Index Index of the member within \p A.
  /// @return False if the member was loaded or is not loadable; true if it
  ///         remains available for normal loading; or an error if the archive
  ///         is invalid.
  LLVM_ABI Expected<bool> operator()(object::Archive &A,
                                     MemoryBufferRef MemberBuf, size_t Index);

private:
  ObjectLayer &L;
  JITDylib &JD;
  bool ObjCOnly;
};

/// Callback that returns fallback CPU type/subtype pairs to try when selecting
/// a MachO slice.
/// @param CPUType Preferred MachO CPU type.
/// @param CPUSubType Preferred MachO CPU subtype.
using GetFallbackArchsFn =
    unique_function<SmallVector<std::pair<uint32_t, uint32_t>>(
        uint32_t CPUType, uint32_t CPUSubType)>;

/// Match the exact CPU type/subtype only.
/// @param CPUType MachO CPU type to match.
/// @param CPUSubType MachO CPU subtype to match.
/// @return An empty list of fallback architectures.
LLVM_ABI SmallVector<std::pair<uint32_t, uint32_t>>
noFallbackArchs(uint32_t CPUType, uint32_t CPUSubType);

/// Match standard dynamic loader fallback rules.
/// @param CPUType Preferred MachO CPU type.
/// @param CPUSubType Preferred MachO CPU subtype.
/// @return Fallback CPU type/subtype pairs to try after the preferred pair.
LLVM_ABI SmallVector<std::pair<uint32_t, uint32_t>>
standardMachOFallbackArchs(uint32_t CPUType, uint32_t CPUSubType);

/// Returns a SymbolNameSet containing the exported symbols defined in the
/// given dylib.
/// @param ES Execution session used to intern symbol names and obtain the
///        target triple.
/// @param Path Path to the MachO dylib or universal binary.
/// @param GetFallbackArchs Callback that supplies fallback architectures when
///        selecting a universal-binary slice.
/// @return Exported symbols from the selected dylib slice, or an error.
LLVM_ABI Expected<SymbolNameSet> getDylibInterfaceFromDylib(
    ExecutionSession &ES, Twine Path,
    GetFallbackArchsFn GetFallbackArchs = standardMachOFallbackArchs);

/// Returns a SymbolNameSet containing the exported symbols defined in the
/// relevant slice of the TapiUniversal file.
/// @param ES Execution session used to intern symbol names and obtain the
///        target triple.
/// @param Path Path to the TAPI interface file.
/// @param GetFallbackArchs Callback that supplies fallback architectures when
///        selecting a compatible slice.
/// @return Exported symbols from the selected TAPI slice, or an error.
LLVM_ABI Expected<SymbolNameSet> getDylibInterfaceFromTapiFile(
    ExecutionSession &ES, Twine Path,
    GetFallbackArchsFn GetFallbackArchs = standardMachOFallbackArchs);

/// Returns a SymbolNameSet containing the exported symbols defined in the
/// relevant slice of the given file, which may be either a dylib or a tapi
/// file.
/// @param ES Execution session used to intern symbol names and obtain the
///        target triple.
/// @param Path Path to the dylib or TAPI interface file.
/// @param GetFallbackArchs Callback that supplies fallback architectures when
///        selecting a compatible slice.
/// @return Exported symbols from the selected dylib or TAPI slice, or an
///         error.
LLVM_ABI Expected<SymbolNameSet> getDylibInterface(
    ExecutionSession &ES, Twine Path,
    GetFallbackArchsFn GetFallbackArchs = standardMachOFallbackArchs);

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_MACHO_H
