//=== MachORelocation.h - Mach-O Relocation Info ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachORelocation class.
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_CODEGEN_MACHORELOCATION_H
#define LLVM_CODEGEN_MACHORELOCATION_H

#include "llvm/Support/DataTypes.h"

namespace llvm {

  /// MachORelocation - This struct contains information about each relocation
  /// that needs to be emitted to the file.
  /// see <mach-o/reloc.h>
  class MachORelocation {
    uint32_t r_address;   // offset in the section to what is being  relocated
    uint32_t r_symbolnum; // symbol index if r_extern == 1 else section index
    bool     r_pcrel;     // was relocated pc-relative already
    uint8_t  r_length;    // length = 2 ^ r_length
    bool     r_extern;    //
    uint8_t  r_type;      // if not 0, machine-specific relocation type.
    bool     r_scattered; // 1 = scattered, 0 = non-scattered
    int32_t  r_value;     // the value the item to be relocated is referring
                          // to.
  public:
    /// Return the relocation fields packed into a Mach-O relocation word.
    /// \return Packed scattered or non-scattered relocation encoding.
    uint32_t getPackedFields() const {
      if (r_scattered)
        return (1 << 31) | (r_pcrel << 30) | ((r_length & 3) << 28) |
          ((r_type & 15) << 24) | (r_address & 0x00FFFFFF);
      else
        return (r_symbolnum << 8) | (r_pcrel << 7) | ((r_length & 3) << 5) |
          (r_extern << 4) | (r_type & 15);
    }
    /// Return the address field appropriate for this relocation kind.
    /// \return \c r_value when scattered, otherwise \c r_address.
    uint32_t getAddress() const { return r_scattered ? r_value : r_address; }
    /// Return the raw section offset stored in \c r_address.
    /// \return Offset in the section of the item being relocated.
    uint32_t getRawAddress() const { return r_address; }

    /// Construct a Mach-O relocation record from its field values.
    /// \param addr Offset in the section to what is being relocated.
    /// \param index Symbol index if \p ext is true, otherwise section index.
    /// \param pcrel Whether the relocation was already applied PC-relative.
    /// \param len Log2 of the relocated item length (\c length = 2 ^ len).
    /// \param ext Whether \p index names an external symbol.
    /// \param type Machine-specific relocation type, or 0 for generic.
    /// \param scattered Whether this is a scattered relocation.
    /// \param value Value referred to by the item being relocated.
    MachORelocation(uint32_t addr, uint32_t index, bool pcrel, uint8_t len,
                    bool ext, uint8_t type, bool scattered = false,
                    int32_t value = 0) :
      r_address(addr), r_symbolnum(index), r_pcrel(pcrel), r_length(len),
      r_extern(ext), r_type(type), r_scattered(scattered), r_value(value) {}
  };

} // end llvm namespace

#endif // LLVM_CODEGEN_MACHORELOCATION_H
