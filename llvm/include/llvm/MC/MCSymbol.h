//===- MCSymbol.h - Machine Code Symbols ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCSymbol class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSYMBOL_H
#define LLVM_MC_MCSYMBOL_H

#include "llvm/ADT/StringMapEntry.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSymbolTableEntry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace llvm {

class MCAsmInfo;
class MCContext;
class MCSection;
class raw_ostream;

/// Represents a symbol name in the MC file.
///
/// MCSymbols are created and uniqued by the MCContext class. MCSymbols
/// should only be constructed with valid names for the object file.
///
/// If the symbol is defined/emitted into the current translation unit, the
/// Section member is set to indicate what section it lives in. Otherwise, if
/// it is a reference to an external entity, it has a null section.
class MCSymbol {
protected:
  /// Discriminator for how this symbol's value is represented.
  enum Kind : uint8_t {
    Regular, ///< Defined relative to a fragment with an offset.
    Equated, ///< Variable symbol equated to an expression.
    Common,  ///< Common symbol with size and alignment.
  };

  /// Sentinel fragment value used for absolute symbols.
  LLVM_ABI static MCFragment *AbsolutePseudoFragment;

  /// Fragment or absolute sentinel that locates this symbol's value.
  ///
  /// If a symbol has a Fragment, the section is implied, so we only need
  /// one pointer.
  /// The special AbsolutePseudoFragment value is for absolute symbols.
  /// If this is a variable symbol, this caches the variable value's fragment.
  /// FIXME: We might be able to simplify this by having the asm streamer create
  /// dummy fragments.
  /// If this is a section, then it gives the symbol is defined in. This is null
  /// for undefined symbols.
  ///
  /// If this is a fragment, then it gives the fragment this symbol's value is
  /// relative to, if any.
  mutable MCFragment *Fragment = nullptr;

  /// The symbol kind. Use an unsigned bitfield to achieve better bitpacking
  /// with MSVC.
  unsigned kind : 2;

  /// True if this symbol is named.  A named symbol will have a pointer to the
  /// name allocated in the bytes immediately prior to the MCSymbol.
  unsigned HasName : 1;

  /// IsTemporary - True if this is an assembler temporary label, which
  /// typically does not survive in the .o file's symbol table.  Usually
  /// "Lfoo" or ".foo".
  unsigned IsTemporary : 1;

  /// True if this symbol can be redefined.
  unsigned IsRedefinable : 1;

  /// True if this symbol has been registered with the context/object writer.
  mutable unsigned IsRegistered : 1;

  /// True if this symbol is visible outside this translation unit. Note: ELF
  /// uses binding instead of this bit.
  mutable unsigned IsExternal : 1;

  /// Mach-O specific: This symbol is private extern.
  mutable unsigned IsPrivateExtern : 1;

  /// This symbol is weak external.
  mutable unsigned IsWeakExternal : 1;

  /// True if we have created a relocation that uses this symbol.
  mutable unsigned IsUsedInReloc : 1;

  /// Used to detect cyclic dependency like `a = a + 1` and `a = b; b = a`.
  unsigned IsResolving : 1;

  /// Number of bits used to encode common-symbol alignment.
  enum : unsigned {
    /// Bits reserved for the log2(align)+1 common-alignment encoding.
    NumCommonAlignmentBits = 5
  };

  /// The alignment of the symbol if it is 'common'.
  ///
  /// Internally, this is stored as log2(align) + 1.
  /// We reserve 5 bits to encode this value which allows the following values
  /// 0b00000 -> unset
  /// 0b00001 -> 1ULL <<  0 = 1
  /// 0b00010 -> 1ULL <<  1 = 2
  /// 0b00011 -> 1ULL <<  2 = 4
  /// ...
  /// 0b11111 -> 1ULL << 30 = 1 GiB
  unsigned CommonAlignLog2 : NumCommonAlignmentBits;

  /// Number of bits reserved for object-file-specific symbol flags.
  enum : unsigned {
    /// Width of the Flags bitfield in bits.
    NumFlagsBits = 16
  };

  /// Object-file-specific per-symbol flags not easily classified elsewhere.
  mutable uint32_t Flags : NumFlagsBits;

  /// Index field, for use by the object file implementation.
  mutable uint32_t Index = 0;

  /// Discriminated storage for offset, common size, or variable expression.
  union {
    /// The offset to apply to the fragment address to form this symbol's value.
    uint64_t Offset;

    /// The size of the symbol, if it is 'common'.
    uint64_t CommonSize;

    /// If non-null, the value for a variable symbol.
    const MCExpr *Value;
  };

  // MCContext creates and uniques these.
  friend class MCExpr;
  friend class MCContext;

  /// Storage preceding an \c MCSymbol for an optional name entry.
  ///
  /// MCSymbol contains a uint64_t so is probably aligned to 8. On a 32-bit
  /// system, the name is a pointer so isn't going to satisfy the 8 byte
  /// alignment of uint64_t. Account for that here.
  using NameEntryStorageTy = union {
    /// Pointer to the symbol's name table entry.
    const MCSymbolTableEntry *NameEntry;
    /// Padding so the name entry meets \c MCSymbol alignment on 32-bit hosts.
    uint64_t AlignmentPadding;
  };

  /// Construct a symbol, optionally named, possibly as a temporary.
  ///
  /// \param Name - Name table entry, or null for an unnamed symbol.
  /// \param isTemporary - True if this is an assembler temporary label.
  MCSymbol(const MCSymbolTableEntry *Name, bool isTemporary)
      : kind(Kind::Regular), IsTemporary(isTemporary), IsRedefinable(false),
        IsRegistered(false), IsExternal(false), IsPrivateExtern(false),
        IsWeakExternal(false), IsUsedInReloc(false), IsResolving(0),
        CommonAlignLog2(0), Flags(0) {
    Offset = 0;
    HasName = !!Name;
    if (Name)
      getNameEntryPtr() = Name;
  }

  /// Copy-construct a symbol.
  ///
  /// \param Other - Symbol to copy.
  MCSymbol(const MCSymbol &Other) = default;
  /// Deleted copy assignment.
  ///
  /// \param Other - Unused; copy assignment is deleted.
  MCSymbol &operator=(const MCSymbol &Other) = delete;

  // Provide custom new/delete as we will only allocate space for a name
  // if we need one.
  /// Allocate an \c MCSymbol, optionally reserving space for a name entry.
  ///
  /// \param s - Size of the \c MCSymbol object.
  /// \param Name - Name table entry, or null if the symbol is unnamed.
  /// \param Ctx - Context used to allocate the symbol.
  /// \return Pointer to the allocated symbol storage.
  LLVM_ABI void *operator new(size_t s, const MCSymbolTableEntry *Name,
                              MCContext &Ctx);

private:
  void operator delete(void *);
  /// Placement delete - required by std, but never called.
  void operator delete(void*, unsigned) {
    llvm_unreachable("Constructor throws?");
  }
  /// Placement delete - required by std, but never called.
  void operator delete(void*, unsigned, bool) {
    llvm_unreachable("Constructor throws?");
  }

  /// Get a reference to the name field.  Requires that we have a name
  const MCSymbolTableEntry *&getNameEntryPtr() {
    assert(HasName && "Name is required");
    NameEntryStorageTy *Name = reinterpret_cast<NameEntryStorageTy *>(this);
    return (*(Name - 1)).NameEntry;
  }
  const MCSymbolTableEntry *&getNameEntryPtr() const {
    return const_cast<MCSymbol*>(this)->getNameEntryPtr();
  }

public:
  /// getName - Get the symbol name.
  ///
  /// \return The symbol name, or an empty string if unnamed.
  StringRef getName() const {
    if (!HasName)
      return StringRef();

    return getNameEntryPtr()->first();
  }

  /// Return true if this symbol has been registered.
  ///
  /// \return True if this symbol has been registered.
  bool isRegistered() const { return IsRegistered; }
  /// Set whether this symbol has been registered.
  ///
  /// \param Value - True if the symbol is registered.
  void setIsRegistered(bool Value) const { IsRegistered = Value; }

  /// Mark this symbol as used in a relocation.
  void setUsedInReloc() const { IsUsedInReloc = true; }
  /// Return true if a relocation has used this symbol.
  ///
  /// \return True if a relocation has used this symbol.
  bool isUsedInReloc() const { return IsUsedInReloc; }

  /// \name Accessors
  /// @{

  /// isTemporary - Check if this is an assembler temporary symbol.
  ///
  /// \return True if this is an assembler temporary symbol.
  bool isTemporary() const { return IsTemporary; }

  /// Check if this symbol is redefinable.
  ///
  /// \return True if this symbol is redefinable.
  bool isRedefinable() const { return IsRedefinable; }
  /// Mark this symbol as redefinable.
  ///
  /// \param Value - True if the symbol may be redefined.
  void setRedefinable(bool Value) { IsRedefinable = Value; }
  /// Prepare this symbol to be redefined.
  void redefineIfPossible() {
    if (IsRedefinable) {
      if (kind == Kind::Equated) {
        Value = nullptr;
        kind = Kind::Regular;
      }
      Fragment = nullptr;
      IsRedefinable = false;
    }
  }

  /// Return whether this symbol is currently being resolved (cycle detection).
  ///
  /// \return True if this symbol is currently being resolved.
  bool isResolving() const { return IsResolving; }
  /// Set whether this symbol is currently being resolved (cycle detection).
  ///
  /// \param V - True if resolution is in progress.
  void setIsResolving(bool V) { IsResolving = V; }

  /// @}
  /// \name Associated Sections
  /// @{

  /// isDefined - Check if this symbol is defined (i.e., it has an address).
  ///
  /// Defined symbols are either absolute or in some section.
  ///
  /// \return True if this symbol is defined.
  bool isDefined() const { return !isUndefined(); }

  /// isInSection - Check if this symbol is defined in some section (i.e., it
  /// is defined but not absolute).
  ///
  /// \return True if this symbol is defined in some section.
  bool isInSection() const {
    auto *F = getFragment();
    return F && F != AbsolutePseudoFragment;
  }

  /// isUndefined - Check if this symbol undefined (i.e., implicitly defined).
  ///
  /// \return True if this symbol is undefined.
  bool isUndefined() const { return getFragment() == nullptr; }

  /// isAbsolute - Check if this is an absolute symbol.
  ///
  /// \return True if this is an absolute symbol.
  bool isAbsolute() const {
    return getFragment() == AbsolutePseudoFragment;
  }

  /// Get the section associated with a defined, non-absolute symbol.
  ///
  /// \return The section this symbol is defined in.
  MCSection &getSection() const {
    assert(isInSection() && "Invalid accessor!");
    return *getFragment()->getParent();
  }

  /// Mark the symbol as defined in the fragment \p F.
  ///
  /// \param F - Fragment that defines this symbol, or null if undefined.
  void setFragment(MCFragment *F) const {
    assert(!isVariable() && "Cannot set fragment of variable");
    Fragment = F;
  }

  /// @}
  /// \name Variable Symbols
  /// @{

  /// isVariable - Check if this is a variable symbol.
  ///
  /// \return True if this is a variable symbol.
  bool isVariable() const { return kind == Equated; }

  /// Get the expression of the variable symbol.
  ///
  /// \return The expression that defines this variable symbol.
  const MCExpr *getVariableValue() const {
    assert(isVariable() && "Invalid accessor!");
    return Value;
  }

  /// Set this symbol as a variable whose value is the given expression.
  ///
  /// \param Value - Expression that defines this variable symbol.
  LLVM_ABI void setVariableValue(const MCExpr *Value);

  /// @}

  /// Get the (implementation defined) index.
  ///
  /// \return The implementation-defined index.
  uint32_t getIndex() const {
    return Index;
  }

  /// Set the (implementation defined) index.
  ///
  /// \param Value - Implementation-defined index to store.
  void setIndex(uint32_t Value) const {
    Index = Value;
  }

  /// Get the byte offset of this regular symbol within its fragment.
  ///
  /// \return The byte offset within the fragment.
  uint64_t getOffset() const {
    assert(kind == Kind::Regular &&
           "Cannot get offset for a common/variable symbol");
    return Offset;
  }
  /// Set the byte offset of this regular symbol within its fragment.
  ///
  /// \param Value - Offset in bytes from the start of the fragment.
  void setOffset(uint64_t Value) {
    assert(kind == Kind::Regular &&
           "Cannot set offset for a common/variable symbol");
    Offset = Value;
  }

  /// Return the size of a 'common' symbol.
  ///
  /// \return The size of the common symbol.
  uint64_t getCommonSize() const {
    assert(isCommon() && "Not a 'common' symbol!");
    return CommonSize;
  }

  /// Mark this symbol as being 'common'.
  ///
  /// \param Size - The size of the symbol.
  /// \param Alignment - The alignment of the symbol.
  void setCommon(uint64_t Size, Align Alignment) {
    assert(getOffset() == 0);
    CommonSize = Size;
    kind = Kind::Common;

    unsigned Log2Align = encode(Alignment);
    assert(Log2Align < (1U << NumCommonAlignmentBits) &&
           "Out of range alignment");
    CommonAlignLog2 = Log2Align;
  }

  /// Return the alignment of a 'common' symbol.
  ///
  /// \return The alignment of the common symbol.
  MaybeAlign getCommonAlignment() const {
    assert(isCommon() && "Not a 'common' symbol!");
    return decodeMaybeAlign(CommonAlignLog2);
  }

  /// Declare this symbol as being 'common'.
  ///
  /// \param Size - The size of the symbol.
  /// \param Alignment - The alignment of the symbol.
  /// \return True if symbol was already declared as a different type
  bool declareCommon(uint64_t Size, Align Alignment) {
    assert(isCommon() || getOffset() == 0);
    if(isCommon()) {
      if (CommonSize != Size || getCommonAlignment() != Alignment)
        return true;
    } else
      setCommon(Size, Alignment);
    return false;
  }

  /// Is this a 'common' symbol.
  ///
  /// \return True if this is a 'common' symbol.
  bool isCommon() const { return kind == Kind::Common; }

  /// Get the fragment this symbol is associated with, resolving aliases.
  ///
  /// \return The associated fragment, or null if undefined.
  MCFragment *getFragment() const {
    if (Fragment || !isVariable() || isWeakExternal())
      return Fragment;
    // If the symbol is a non-weak alias, get information about
    // the aliasee. (Don't try to resolve weak aliases.)
    Fragment = getVariableValue()->findAssociatedFragment();
    return Fragment;
  }

  /// Return true if this is a COFF weak-external symbol.
  ///
  /// \return True if this is a COFF weak-external symbol.
  bool isWeakExternal() const { return IsWeakExternal; }

  /// Print the symbol name to the stream \p OS.
  ///
  /// \param OS - Stream to print to.
  /// \param MAI - Optional assembler info for target-specific formatting.
  LLVM_ABI void print(raw_ostream &OS, const MCAsmInfo *MAI) const;
  /// Print the symbol name to the stream \p OS using \p MAI.
  ///
  /// \param OS - Stream to print to.
  /// \param MAI - Assembler info for target-specific formatting.
  void print(raw_ostream &OS, const MCAsmInfo &MAI) const { print(OS, &MAI); }

  /// dump - Print the value to stderr.
  LLVM_ABI void dump() const;

protected:
  /// Get the (implementation defined) symbol flags.
  ///
  /// \return The implementation-defined symbol flags.
  uint32_t getFlags() const { return Flags; }

  /// Set the (implementation defined) symbol flags.
  ///
  /// \param Value - New flags value.
  void setFlags(uint32_t Value) const {
    assert(Value < (1U << NumFlagsBits) && "Out of range flags");
    Flags = Value;
  }

  /// Modify the flags via a mask.
  ///
  /// \param Value - Flag bits to set within \p Mask.
  /// \param Mask - Bits of \c Flags to replace with \p Value.
  void modifyFlags(uint32_t Value, uint32_t Mask) const {
    assert(Value < (1U << NumFlagsBits) && "Out of range flags");
    Flags = (Flags & ~Mask) | Value;
  }
};

/// Print \p Sym to \p OS without target-specific assembler info.
///
/// \param OS - Stream to print to.
/// \param Sym - Symbol to print.
/// \return The output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const MCSymbol &Sym) {
  Sym.print(OS, nullptr);
  return OS;
}

/// Return true if the distance between \p Begin and \p End may change at link
/// time.
///
/// \param Begin - Start symbol of the range.
/// \param End - End symbol of the range.
/// \return True if the range may change size at link time.
LLVM_ABI bool isRangeRelaxable(const MCSymbol *Begin, const MCSymbol *End);

} // end namespace llvm

#endif // LLVM_MC_MCSYMBOL_H
