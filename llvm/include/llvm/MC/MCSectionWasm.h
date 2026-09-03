//===- MCSectionWasm.h - Wasm Machine Code Sections -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionWasm class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONWASM_H
#define LLVM_MC_MCSECTIONWASM_H

#include "llvm/MC/MCSection.h"

namespace llvm {

class MCSymbol;
class MCSymbolWasm;
class StringRef;
class raw_ostream;

/// This represents a section on wasm.
class MCSectionWasm final : public MCSection {
  unsigned UniqueID;

  const MCSymbolWasm *Group;

  // The offset of the MC function/data section in the wasm code/data section.
  // For data relocations the offset is relative to start of the data payload
  // itself and does not include the size of the section header.
  uint64_t SectionOffset = 0;

  // For data sections, this is the index of the corresponding wasm data
  // segment
  uint32_t SegmentIndex = 0;

  // For data sections, whether to use a passive segment
  bool IsPassive = false;

  bool IsWasmData;

  bool IsMetadata;

  // For data sections, bitfield of WasmSegmentFlag
  unsigned SegmentFlags;

  // The storage of Name is owned by MCContext's WasmUniquingMap.
  friend class MCContext;
  friend class MCAsmInfoWasm;
  MCSectionWasm(StringRef Name, SectionKind K, unsigned SegmentFlags,
                const MCSymbolWasm *Group, unsigned UniqueID, MCSymbol *Begin)
      : MCSection(Name, K.isText(), /*IsVirtual=*/false, Begin),
        UniqueID(UniqueID), Group(Group),
        IsWasmData(K.isReadOnly() || K.isWriteable()),
        IsMetadata(K.isMetadata()), SegmentFlags(SegmentFlags) {}

public:
  /// Return the section group signature symbol, or null if none.
  /// @return The section group signature symbol, or null if none.
  const MCSymbolWasm *getGroup() const { return Group; }
  /// Return the Wasm segment flags bitfield (WasmSegmentFlag).
  /// @return The Wasm segment flags bitfield (WasmSegmentFlag).
  unsigned getSegmentFlags() const { return SegmentFlags; }

  /// Return true if this is a Wasm data section (read-only or writable).
  /// @return True if this is a Wasm data section (read-only or writable).
  bool isWasmData() const { return IsWasmData; }
  /// Return true if this is a Wasm metadata section.
  /// @return True if this is a Wasm metadata section.
  bool isMetadata() const { return IsMetadata; }

  /// Return true if this section was created with a unique ID.
  /// @return True if this section was created with a unique ID.
  bool isUnique() const { return UniqueID != ~0U; }
  /// Return the unique ID assigned to this section.
  /// @return The unique ID assigned to this section.
  unsigned getUniqueID() const { return UniqueID; }

  /// Return the offset of this section within the Wasm code/data section.
  /// @return The offset of this section within the Wasm code/data section.
  uint64_t getSectionOffset() const { return SectionOffset; }
  /// Set the offset of this section within the Wasm code/data section.
  /// @param Offset Offset relative to the start of the data payload.
  void setSectionOffset(uint64_t Offset) { SectionOffset = Offset; }

  /// Return the Wasm data segment index for this data section.
  /// @return The Wasm data segment index for this data section.
  uint32_t getSegmentIndex() const { return SegmentIndex; }
  /// Set the Wasm data segment index for this data section.
  /// @param Index Index of the corresponding Wasm data segment.
  void setSegmentIndex(uint32_t Index) { SegmentIndex = Index; }

  /// Return true if this data section uses a passive Wasm segment.
  /// @return True if this data section uses a passive Wasm segment.
  bool getPassive() const {
    assert(isWasmData());
    return IsPassive;
  }
  /// Set whether this data section uses a passive Wasm segment.
  /// @param V True to use a passive segment; defaults to true.
  void setPassive(bool V = true) {
    assert(isWasmData());
    IsPassive = V;
  }
};

} // end namespace llvm

#endif
