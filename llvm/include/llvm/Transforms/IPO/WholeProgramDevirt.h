//===- WholeProgramDevirt.h - Whole-program devirt pass ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines parts of the whole-program devirtualization pass
// implementation that may be usefully unit tested.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_WHOLEPROGRAMDEVIRT_H
#define LLVM_TRANSFORMS_IPO_WHOLEPROGRAMDEVIRT_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace llvm {
class Module;

template <typename T> class ArrayRef;
template <typename T> class MutableArrayRef;
class GlobalVariable;
class ModuleSummaryIndex;
struct ValueInfo;

/// Helpers and data structures for the whole-program devirtualization pass.
namespace wholeprogramdevirt {

/// A bit vector that tracks which bits are used when packing constants.
///
/// We use this to pack constant values compactly before and after each virtual
/// table.
struct AccumBitVector {
  /// Packed constant bytes stored so far.
  std::vector<uint8_t> Bytes;

  /// Mask of used bits in \c Bytes; bit I is 1 if \c Bytes[I] is used.
  std::vector<uint8_t> BytesUsed;

  /// Return pointers into \c Bytes and \c BytesUsed for \p Size bytes at \p Pos.
  ///
  /// Resizes both vectors if needed so that \p Pos + \p Size is in range.
  ///
  /// \param Pos Byte offset at which to access the packed data.
  /// \param Size Number of bytes to reserve and return pointers for.
  /// \return Pointers to \c Bytes and \c BytesUsed at \p Pos.
  std::pair<uint8_t *, uint8_t *> getPtrToData(uint64_t Pos, uint8_t Size) {
    if (Bytes.size() < Pos + Size) {
      Bytes.resize(Pos + Size);
      BytesUsed.resize(Pos + Size);
    }
    return std::make_pair(Bytes.data() + Pos, BytesUsed.data() + Pos);
  }

  /// Store little-endian value \p Val of \p Size bytes at bit position \p Pos.
  ///
  /// Marks the corresponding bytes as used.
  ///
  /// \param Pos Bit position at which to store; must be byte-aligned.
  /// \param Val Value to store in little-endian byte order.
  /// \param Size Number of bytes to write.
  void setLE(uint64_t Pos, uint64_t Val, uint8_t Size) {
    assert(Pos % 8 == 0);
    auto DataUsed = getPtrToData(Pos / 8, Size);
    for (unsigned I = 0; I != Size; ++I) {
      DataUsed.first[I] = Val >> (I * 8);
      assert(!DataUsed.second[I]);
      DataUsed.second[I] = 0xff;
    }
  }

  /// Store big-endian value \p Val of \p Size bytes at bit position \p Pos.
  ///
  /// Marks the corresponding bytes as used.
  ///
  /// \param Pos Bit position at which to store; must be byte-aligned.
  /// \param Val Value to store in big-endian byte order.
  /// \param Size Number of bytes to write.
  void setBE(uint64_t Pos, uint64_t Val, uint8_t Size) {
    assert(Pos % 8 == 0);
    auto DataUsed = getPtrToData(Pos / 8, Size);
    for (unsigned I = 0; I != Size; ++I) {
      DataUsed.first[Size - I - 1] = Val >> (I * 8);
      assert(!DataUsed.second[Size - I - 1]);
      DataUsed.second[Size - I - 1] = 0xff;
    }
  }

  /// Set the bit at bit position \p Pos to \p b and mark it as used.
  ///
  /// \param Pos Bit position to set within the packed byte array.
  /// \param b Value to store at that bit position.
  void setBit(uint64_t Pos, bool b) {
    auto DataUsed = getPtrToData(Pos / 8, 1);
    if (b)
      *DataUsed.first |= 1 << (Pos % 8);
    assert(!(*DataUsed.second & (1 << Pos % 8)));
    *DataUsed.second |= 1 << (Pos % 8);
  }
};

/// Bits stored before and after a particular vtable.
struct VTableBits {
  /// The vtable global.
  GlobalVariable *GV;

  /// Cached size of the vtable object in bytes.
  uint64_t ObjectSize = 0;

  /// Bit vector laid out before the vtable.
  ///
  /// These bytes are stored in reverse order until the globals are rebuilt.
  /// Any values in the array must therefore be stored using the opposite
  /// endianness from the target.
  AccumBitVector Before;

  /// Bit vector laid out after the vtable.
  AccumBitVector After;
};

/// Information about a member of a particular type identifier.
struct TypeMemberInfo {
  /// The \c VTableBits for the vtable.
  VTableBits *Bits;

  /// Offset in bytes from the start of the vtable (the address point).
  uint64_t Offset;

  /// Compare type members by vtable bits pointer and offset.
  ///
  /// \param other Other type member to compare against.
  /// \return True if this member precedes \p other in (Bits, Offset) order.
  bool operator<(const TypeMemberInfo &other) const {
    return std::tie(Bits, Offset) < std::tie(other.Bits, other.Offset);
  }
};

/// A virtual call target, i.e. an entry in a particular vtable.
struct VirtualCallTarget {
  /// Construct a virtual call target for function \p Fn via member \p TM.
  ///
  /// \param Fn Function (or alias to a function) stored in the vtable.
  /// \param TM Type-identifier member through which \p Fn is accessed.
  LLVM_ABI VirtualCallTarget(GlobalValue *Fn, const TypeMemberInfo *TM);

  /// Construct a virtual call target for testing.
  ///
  /// \param TM Type-identifier member describing the vtable slot.
  /// \param IsBigEndian Whether the target is big endian.
  VirtualCallTarget(const TypeMemberInfo *TM, bool IsBigEndian)
      : Fn(nullptr), TM(TM), IsBigEndian(IsBigEndian), WasDevirt(false) {}

  /// The function (or an alias to a function) stored in the vtable.
  GlobalValue *Fn;

  /// Type-identifier member through which the pointer to \c Fn is accessed.
  const TypeMemberInfo *TM;

  /// Return value used during virtual constant propagation.
  ///
  /// Stores the return value for the function when passed the currently
  /// considered argument list.
  uint64_t RetVal;

  /// Whether the target is big endian.
  bool IsBigEndian;

  /// Whether at least one call site to the target was devirtualized.
  bool WasDevirt;

  /// Return the minimum byte offset before the address point.
  ///
  /// This covers the bytes in the vtable object before the address point (e.g.
  /// RTTI, access-to-top, vtables for other base classes) and is equal to the
  /// offset from the start of the vtable object to the address point.
  ///
  /// \return Bytes from the start of the vtable object to the address point.
  uint64_t minBeforeBytes() const { return TM->Offset; }

  /// Return the minimum byte offset after the address point.
  ///
  /// This covers the bytes in the vtable object after the address point (e.g.
  /// the vtable for the current class and any later base classes) and is equal
  /// to the size of the vtable object minus the offset from the start of the
  /// vtable object to the address point.
  ///
  /// \return Bytes from the address point to the end of the vtable object.
  uint64_t minAfterBytes() const { return TM->Bits->ObjectSize - TM->Offset; }

  /// Return bytes allocated before the address point for the vtable plus array.
  ///
  /// \return Bytes before the address point plus bytes in the before bit
  ///         vector.
  uint64_t allocatedBeforeBytes() const {
    return minBeforeBytes() + TM->Bits->Before.Bytes.size();
  }

  /// Return bytes allocated after the address point for the vtable plus array.
  ///
  /// \return Bytes after the address point plus bytes in the after bit vector.
  uint64_t allocatedAfterBytes() const {
    return minAfterBytes() + TM->Bits->After.Bytes.size();
  }

  /// Set the bit at position \p Pos before the address point to \c RetVal.
  ///
  /// \param Pos Bit position before the address point to update.
  void setBeforeBit(uint64_t Pos) {
    assert(Pos >= 8 * minBeforeBytes());
    TM->Bits->Before.setBit(Pos - 8 * minBeforeBytes(), RetVal);
  }

  /// Set the bit at position \p Pos after the address point to \c RetVal.
  ///
  /// \param Pos Bit position after the address point to update.
  void setAfterBit(uint64_t Pos) {
    assert(Pos >= 8 * minAfterBytes());
    TM->Bits->After.setBit(Pos - 8 * minAfterBytes(), RetVal);
  }

  /// Set \p Size bytes at position \p Pos before the address point to \c RetVal.
  ///
  /// Because the bytes in \c Before are stored in reverse order, this uses the
  /// opposite endianness to the target.
  ///
  /// \param Pos Bit position before the address point at which to store.
  /// \param Size Number of bytes to write from \c RetVal.
  void setBeforeBytes(uint64_t Pos, uint8_t Size) {
    assert(Pos >= 8 * minBeforeBytes());
    if (IsBigEndian)
      TM->Bits->Before.setLE(Pos - 8 * minBeforeBytes(), RetVal, Size);
    else
      TM->Bits->Before.setBE(Pos - 8 * minBeforeBytes(), RetVal, Size);
  }

  /// Set \p Size bytes at position \p Pos after the address point to \c RetVal.
  ///
  /// \param Pos Bit position after the address point at which to store.
  /// \param Size Number of bytes to write from \c RetVal.
  void setAfterBytes(uint64_t Pos, uint8_t Size) {
    assert(Pos >= 8 * minAfterBytes());
    if (IsBigEndian)
      TM->Bits->After.setBE(Pos - 8 * minAfterBytes(), RetVal, Size);
    else
      TM->Bits->After.setLE(Pos - 8 * minAfterBytes(), RetVal, Size);
  }
};

/// Find the lowest offset at which a value of \p Size bits may be stored.
///
/// If \p IsAfter is set, look for an offset after the object; otherwise look
/// for an offset before the object.
///
/// \param Targets Virtual call targets whose layouts constrain the offset.
/// \param IsAfter Whether to search after the object rather than before it.
/// \param Size Size in bits of the value to place.
/// \return The lowest bit offset at which the value may be stored.
LLVM_ABI uint64_t findLowestOffset(ArrayRef<VirtualCallTarget> Targets,
                                   bool IsAfter, uint64_t Size);

/// Store each target's \c RetVal at allocation offset \p AllocBefore.
///
/// Writes values before the vtable address and stores the computed byte/bit
/// offset in \p OffsetByte / \p OffsetBit.
///
/// \param Targets Virtual call targets that receive the stored return values.
/// \param AllocBefore Allocation offset before the vtable address.
/// \param BitWidth Width in bits of the stored return value.
/// \param OffsetByte Set to the computed byte offset of the stored value.
/// \param OffsetBit Set to the computed bit offset within that byte.
LLVM_ABI void setBeforeReturnValues(MutableArrayRef<VirtualCallTarget> Targets,
                                    uint64_t AllocBefore, unsigned BitWidth,
                                    int64_t &OffsetByte, uint64_t &OffsetBit);

/// Store each target's \c RetVal at allocation offset \p AllocAfter.
///
/// Writes values after the vtable address and stores the computed byte/bit
/// offset in \p OffsetByte / \p OffsetBit.
///
/// \param Targets Virtual call targets that receive the stored return values.
/// \param AllocAfter Allocation offset after the vtable address.
/// \param BitWidth Width in bits of the stored return value.
/// \param OffsetByte Set to the computed byte offset of the stored value.
/// \param OffsetBit Set to the computed bit offset within that byte.
LLVM_ABI void setAfterReturnValues(MutableArrayRef<VirtualCallTarget> Targets,
                                   uint64_t AllocAfter, unsigned BitWidth,
                                   int64_t &OffsetByte, uint64_t &OffsetBit);

} // end namespace wholeprogramdevirt

/// Pass that performs whole-program virtual call devirtualization.
struct WholeProgramDevirtPass
    : public OptionalPassInfoMixin<WholeProgramDevirtPass> {
  /// Summary used when exporting whole-program-devirt resolutions.
  ModuleSummaryIndex *ExportSummary;
  /// Summary used when importing whole-program-devirt resolutions.
  const ModuleSummaryIndex *ImportSummary;
  /// Whether pass options should be read from the command line.
  bool UseCommandLine = false;
  /// Whether to attempt speculative (non-must) devirtualization.
  bool DevirtSpeculatively = false;

  /// Construct a pass that reads options from the command line.
  WholeProgramDevirtPass()
      : ExportSummary(nullptr), ImportSummary(nullptr), UseCommandLine(true) {}

  /// Construct a pass with the given export and import summaries.
  ///
  /// \param ExportSummary Summary used when exporting WPD resolutions.
  /// \param ImportSummary Summary used when importing WPD resolutions.
  /// \param DevirtSpeculatively Whether to attempt speculative devirtualization.
  WholeProgramDevirtPass(ModuleSummaryIndex *ExportSummary,
                         const ModuleSummaryIndex *ImportSummary,
                         bool DevirtSpeculatively = false)
      : ExportSummary(ExportSummary), ImportSummary(ImportSummary),
        DevirtSpeculatively(DevirtSpeculatively) {
    assert(!(ExportSummary && ImportSummary));
  }

  /// Run whole-program devirtualization over the given module.
  ///
  /// \param M Module whose virtual calls may be devirtualized.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Summary of a vtable slot identified by type ID and byte offset.
struct VTableSlotSummary {
  /// Type identifier metadata string for the vtable slot.
  StringRef TypeID;
  /// Byte offset of the slot within the vtable.
  uint64_t ByteOffset;
};

/// Return whether whole-program visibility is enabled for this LTO link.
///
/// \param WholeProgramVisibilityEnabledInLTO Whether LTO enabled whole-program
///        visibility.
/// \return True if whole-program visibility is enabled for this LTO link.
LLVM_ABI bool
hasWholeProgramVisibility(bool WholeProgramVisibilityEnabledInLTO);

/// Update public type-test calls according to whole-program visibility.
///
/// \param M Module whose public type-test calls are updated.
/// \param WholeProgramVisibilityEnabledInLTO Whether LTO enabled whole-program
///        visibility.
LLVM_ABI void
updatePublicTypeTestCalls(Module &M, bool WholeProgramVisibilityEnabledInLTO);

/// Update virtual-call visibility metadata on vtables in \p M.
///
/// \param M Module whose vtable visibility metadata is updated.
/// \param WholeProgramVisibilityEnabledInLTO Whether LTO enabled whole-program
///        visibility.
/// \param DynamicExportSymbols GUIDs of symbols that may be exported
///        dynamically.
/// \param ValidateAllVtablesHaveTypeInfos Whether to require type info on all
///        vtables.
/// \param IsVisibleToRegularObj Predicate that returns true if a symbol name is
///        visible to regular object files.
LLVM_ABI void updateVCallVisibilityInModule(
    Module &M, bool WholeProgramVisibilityEnabledInLTO,
    const DenseSet<GlobalValue::GUID> &DynamicExportSymbols,
    bool ValidateAllVtablesHaveTypeInfos,
    function_ref<bool(StringRef)> IsVisibleToRegularObj);

/// Update virtual-call visibility in the module summary index.
///
/// \param Index Module summary index whose vtable visibility is updated.
/// \param WholeProgramVisibilityEnabledInLTO Whether LTO enabled whole-program
///        visibility.
/// \param DynamicExportSymbols GUIDs of symbols that may be exported
///        dynamically.
/// \param VisibleToRegularObjSymbols GUIDs of vtables visible to regular
///        object files.
LLVM_ABI void updateVCallVisibilityInIndex(
    ModuleSummaryIndex &Index, bool WholeProgramVisibilityEnabledInLTO,
    const DenseSet<GlobalValue::GUID> &DynamicExportSymbols,
    const DenseSet<GlobalValue::GUID> &VisibleToRegularObjSymbols);

/// Collect GUIDs of vtables visible to regular object files.
///
/// \param Index Module summary index whose vtables are examined.
/// \param VisibleToRegularObjSymbols Set populated with GUIDs of vtables
///        visible to regular object files.
/// \param IsVisibleToRegularObj Predicate that returns true if a symbol name is
///        visible to regular object files.
LLVM_ABI void getVisibleToRegularObjVtableGUIDs(
    ModuleSummaryIndex &Index,
    DenseSet<GlobalValue::GUID> &VisibleToRegularObjSymbols,
    function_ref<bool(StringRef)> IsVisibleToRegularObj);

/// Perform index-based whole-program devirtualization on \p Summary.
///
/// Any devirtualized targets used by a type test in another module are added
/// to the \p ExportedGUIDs set. For any local devirtualized targets only used
/// within the defining module, the information necessary for locating the
/// corresponding WPD resolution is recorded for the ValueInfo in case it is
/// exported by cross-module importing (in which case the devirtualized target
/// name will need adjustment).
///
/// \param Summary Combined module summary index to run WPD on.
/// \param ExportedGUIDs Set of GUIDs that must be exported for cross-module use.
/// \param LocalWPDTargetsMap Map from local ValueInfo entries to the vtable
///        slots whose WPD resolutions may need renaming if exported.
/// \param ExternallyVisibleSymbolNamesPtr Optional set of symbol names that
///        remain externally visible; may be null.
LLVM_ABI void runWholeProgramDevirtOnIndex(
    ModuleSummaryIndex &Summary, std::set<GlobalValue::GUID> &ExportedGUIDs,
    std::map<ValueInfo, std::vector<VTableSlotSummary>> &LocalWPDTargetsMap,
    DenseSet<StringRef> *ExternallyVisibleSymbolNamesPtr = nullptr);

/// Update recorded single-impl WPD target names after cross-module importing.
///
/// Call after cross-module importing to update the recorded single-impl
/// devirtualization target names for any locals that were exported.
///
/// \param Summary Combined module summary index whose WPD resolutions are
///        updated.
/// \param isExported Predicate that returns true if a symbol with the given
///        name and ValueInfo was exported.
/// \param LocalWPDTargetsMap Map from local ValueInfo entries to the vtable
///        slots whose recorded target names may need updating.
/// \param ExternallyVisibleSymbolNamesPtr Optional set of symbol names that
///        remain externally visible; may be null.
LLVM_ABI void updateIndexWPDForExports(
    ModuleSummaryIndex &Summary,
    function_ref<bool(StringRef, ValueInfo)> isExported,
    std::map<ValueInfo, std::vector<VTableSlotSummary>> &LocalWPDTargetsMap,
    DenseSet<StringRef> *ExternallyVisibleSymbolNamesPtr = nullptr);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_WHOLEPROGRAMDEVIRT_H
