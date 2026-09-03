//===- MCSymbolMachO.h -  ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCSYMBOLMACHO_H
#define LLVM_MC_MCSYMBOLMACHO_H

#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolTableEntry.h"

namespace llvm {
/// Mach-O MC symbol that stores nlist desc flags in the symbol flags field.
class MCSymbolMachO : public MCSymbol {
  /// We store the value for the 'desc' symbol field in the
  /// lowest 16 bits of the implementation defined flags.
  enum MachOSymbolFlags : uint16_t { // See <mach-o/nlist.h>.
    SF_DescFlagsMask                        = 0xFFFF,

    // Reference type flags.
    SF_ReferenceTypeMask                    = 0x0007,
    SF_ReferenceTypeUndefinedNonLazy        = 0x0000,
    SF_ReferenceTypeUndefinedLazy           = 0x0001,
    SF_ReferenceTypeDefined                 = 0x0002,
    SF_ReferenceTypePrivateDefined          = 0x0003,
    SF_ReferenceTypePrivateUndefinedNonLazy = 0x0004,
    SF_ReferenceTypePrivateUndefinedLazy    = 0x0005,

    // Other 'desc' flags.
    SF_ThumbFunc                            = 0x0008,
    SF_NoDeadStrip                          = 0x0020,
    SF_WeakReference                        = 0x0040,
    SF_WeakDefinition                       = 0x0080,
    SF_SymbolResolver                       = 0x0100,
    SF_AltEntry                             = 0x0200,
    SF_Cold                                 = 0x0400,

    // Common alignment
    SF_CommonAlignmentMask                  = 0xF0FF,
    SF_CommonAlignmentShift                 = 8
  };

public:
  /// Construct a Mach-O symbol, optionally named, possibly as a temporary.
  ///
  /// \param Name - Name table entry, or null for an unnamed symbol.
  /// \param isTemporary - True if this is an assembler temporary label.
  MCSymbolMachO(const MCSymbolTableEntry *Name, bool isTemporary)
      : MCSymbol(Name, isTemporary) {}

  /// Return true if this symbol is visible outside this translation unit.
  ///
  /// \return True if the symbol is external.
  bool isExternal() const { return IsExternal; }
  /// Set whether this symbol is visible outside this translation unit.
  ///
  /// \param Value - True if the symbol is external.
  void setExternal(bool Value) const { IsExternal = Value; }
  /// Return true if this symbol is private extern (N_PEXT).
  ///
  /// \return True if the symbol is private extern.
  bool isPrivateExtern() const { return IsPrivateExtern; }
  /// Set whether this symbol is private extern (N_PEXT).
  ///
  /// \param Value - True if the symbol is private extern.
  void setPrivateExtern(bool Value) { IsPrivateExtern = Value; }

  // Reference type methods.

  /// Clear the Mach-O reference-type bits in the desc flags.
  void clearReferenceType() const {
    modifyFlags(0, SF_ReferenceTypeMask);
  }

  /// Set or clear the undefined-lazy reference type bit.
  ///
  /// \param Value - True to mark the reference as undefined lazy.
  void setReferenceTypeUndefinedLazy(bool Value) const {
    modifyFlags(Value ? SF_ReferenceTypeUndefinedLazy : 0,
                SF_ReferenceTypeUndefinedLazy);
  }

  // Other 'desc' methods.

  /// Mark this symbol as a Thumb function (N_ARM_THUMB_DEF).
  void setThumbFunc() const {
    modifyFlags(SF_ThumbFunc, SF_ThumbFunc);
  }

  /// Return true if this symbol must not be dead-stripped.
  ///
  /// \return True if the symbol must not be dead-stripped.
  bool isNoDeadStrip() const {
    return getFlags() & SF_NoDeadStrip;
  }
  /// Mark this symbol so the linker will not dead-strip it.
  void setNoDeadStrip() const {
    modifyFlags(SF_NoDeadStrip, SF_NoDeadStrip);
  }

  /// Return true if this is a weak reference (N_WEAK_REF).
  ///
  /// \return True if the symbol is a weak reference.
  bool isWeakReference() const {
    return getFlags() & SF_WeakReference;
  }
  /// Mark this symbol as a weak reference (N_WEAK_REF).
  void setWeakReference() const {
    modifyFlags(SF_WeakReference, SF_WeakReference);
  }

  /// Return true if this is a weak definition (N_WEAK_DEF).
  ///
  /// \return True if the symbol is a weak definition.
  bool isWeakDefinition() const {
    return getFlags() & SF_WeakDefinition;
  }
  /// Mark this symbol as a weak definition (N_WEAK_DEF).
  void setWeakDefinition() const {
    modifyFlags(SF_WeakDefinition, SF_WeakDefinition);
  }

  /// Return true if this symbol is a symbol resolver.
  ///
  /// \return True if the symbol is a symbol resolver.
  bool isSymbolResolver() const {
    return getFlags() & SF_SymbolResolver;
  }
  /// Mark this symbol as a symbol resolver (N_SYMBOL_RESOLVER).
  void setSymbolResolver() const {
    modifyFlags(SF_SymbolResolver, SF_SymbolResolver);
  }

  /// Mark this symbol as an alternate entry point (N_ALT_ENTRY).
  void setAltEntry() const {
    modifyFlags(SF_AltEntry, SF_AltEntry);
  }

  /// Return true if this symbol is an alternate entry point.
  ///
  /// \return True if the symbol is an alternate entry point.
  bool isAltEntry() const {
    return getFlags() & SF_AltEntry;
  }

  /// Mark this symbol as a cold function (N_COLD_FUNC).
  void setCold() const { modifyFlags(SF_Cold, SF_Cold); }

  /// Return true if this symbol is marked as a cold function.
  ///
  /// \return True if the symbol is marked cold.
  bool isCold() const { return getFlags() & SF_Cold; }

  /// Set the raw Mach-O nlist desc value for this symbol.
  ///
  /// \param Value - Desc bits to store; must fit in \c SF_DescFlagsMask.
  void setDesc(unsigned Value) const {
    assert(Value == (Value & SF_DescFlagsMask) &&
           "Invalid .desc value!");
    setFlags(Value & SF_DescFlagsMask);
  }

  /// Return whether this symbol is visible to the linker and required in the
  /// symbol table.
  ///
  /// Otherwise the assembler may discard it. This also affects whether the
  /// assembler treats the label as potentially defining a separate atom.
  ///
  /// \return True if the symbol is linker-visible.
  bool isSymbolLinkerVisible() const {
    // Non-temporary labels should always be visible to the linker.
    if (!isTemporary())
      return true;

    return isUsedInReloc();
  }

  /// Get the encoded value of the flags as they will be emitted in to
  /// the MachO binary
  ///
  /// \param EncodeAsAltEntry - True to force the alt-entry flag in the result.
  /// \return The encoded Mach-O nlist desc flags.
  uint16_t getEncodedFlags(bool EncodeAsAltEntry) const {
    uint16_t Flags = getFlags();

    // Common alignment is packed into the 'desc' bits.
    if (isCommon()) {
      if (MaybeAlign MaybeAlignment = getCommonAlignment()) {
        Align Alignment = *MaybeAlignment;
        unsigned Log2Size = Log2(Alignment);
        if (Log2Size > 15)
          report_fatal_error("invalid 'common' alignment '" +
                                 Twine(Alignment.value()) + "' for '" +
                                 getName() + "'",
                             false);
        Flags = (Flags & SF_CommonAlignmentMask) |
                (Log2Size << SF_CommonAlignmentShift);
      }
    }

    if (EncodeAsAltEntry)
      Flags |= SF_AltEntry;

    return Flags;
  }
};
}

#endif
