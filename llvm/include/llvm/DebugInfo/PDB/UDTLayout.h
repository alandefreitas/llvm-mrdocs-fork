//===- UDTLayout.h - UDT layout info ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_UDTLAYOUT_H
#define LLVM_DEBUGINFO_PDB_UDTLAYOUT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBaseClass.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeVTable.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
namespace pdb {

class BaseClassLayout;
class ClassLayout;
class UDTLayoutBase;

/// Base class for a field or subobject in a UDT memory layout.
class LLVM_ABI LayoutItemBase {
public:
  /// Construct a layout item nested under \p Parent.
  /// \param Parent Containing UDT layout, or null for a top-level item.
  /// \param Symbol PDB symbol this item represents, if any.
  /// \param Name Display name of the layout item.
  /// \param OffsetInParent Byte offset of this item within its parent.
  /// \param Size Size in bytes of this item.
  /// \param IsElided True if this item is omitted from physical layout.
  LayoutItemBase(const UDTLayoutBase *Parent, const PDBSymbol *Symbol,
                 const std::string &Name, uint32_t OffsetInParent,
                 uint32_t Size, bool IsElided);
  /// Destroy the layout item.
  virtual ~LayoutItemBase() = default;

  /// Return the number of unused bytes within this item's extent.
  /// \returns The number of unused bytes within this item's extent.
  uint32_t deepPaddingSize() const;
  /// Return padding immediately introduced by this item (default 0).
  /// \returns Immediate padding in bytes; the base implementation returns 0.
  virtual uint32_t immediatePadding() const { return 0; }
  /// Return unused bytes at the end of this item's extent.
  /// \returns The number of unused trailing bytes in this item's extent.
  virtual uint32_t tailPadding() const;

  /// Return the containing UDT layout, or null if none.
  /// \returns The parent UDT layout, or null if this is a top-level item.
  const UDTLayoutBase *getParent() const { return Parent; }
  /// Return the display name of this layout item.
  /// \returns The display name of this layout item.
  StringRef getName() const { return Name; }
  /// Return the byte offset of this item within its parent.
  /// \returns The byte offset of this item within its parent.
  uint32_t getOffsetInParent() const { return OffsetInParent; }
  /// Return the size in bytes of this item.
  /// \returns The size in bytes of this item.
  uint32_t getSize() const { return SizeOf; }
  /// Return the laid-out size in bytes of this item.
  /// \returns The laid-out size in bytes of this item.
  uint32_t getLayoutSize() const { return LayoutSize; }
  /// Return the PDB symbol associated with this item, if any.
  /// \returns The associated PDB symbol, or null if none.
  const PDBSymbol *getSymbol() const { return Symbol; }
  /// Return the bit vector of bytes occupied by this item.
  /// \returns The bit vector of bytes occupied by this item.
  const BitVector &usedBytes() const { return UsedBytes; }
  /// Return true if this item is elided from physical layout.
  /// \returns True if this item is elided from physical layout.
  bool isElided() const { return IsElided; }
  /// Return true if this item is a virtual base pointer.
  /// \returns True if this item is a virtual base pointer.
  virtual bool isVBPtr() const { return false; }

  /// Return true if \p Off falls within this item's byte range in its parent.
  /// \param Off Offset in the parent to test.
  /// \returns Non-zero if \p Off is within this item's range; otherwise zero.
  uint32_t containsOffset(uint32_t Off) const {
    uint32_t Begin = getOffsetInParent();
    uint32_t End = Begin + getSize();
    return (Off >= Begin && Off < End);
  }

protected:
  /// PDB symbol this layout item represents, if any.
  const PDBSymbol *Symbol = nullptr;
  /// Containing UDT layout, or null for a top-level item.
  const UDTLayoutBase *Parent = nullptr;
  /// Bit vector of bytes occupied within this item's size.
  BitVector UsedBytes;
  /// Display name of this layout item.
  std::string Name;
  /// Byte offset of this item within its parent.
  uint32_t OffsetInParent = 0;
  /// Size in bytes of this item.
  uint32_t SizeOf = 0;
  /// Laid-out size in bytes of this item.
  uint32_t LayoutSize = 0;
  /// True if this item is omitted from physical layout.
  bool IsElided = false;
};

/// Layout item representing a virtual base class pointer (vbptr).
class VBPtrLayoutItem : public LayoutItemBase {
public:
  /// Construct a vbptr layout item under \p Parent.
  /// \param Parent Containing UDT layout.
  /// \param Sym Builtin type symbol for the vbptr.
  /// \param Offset Byte offset of the vbptr within the parent.
  /// \param Size Size in bytes of the vbptr.
  LLVM_ABI VBPtrLayoutItem(const UDTLayoutBase &Parent,
                           std::unique_ptr<PDBSymbolTypeBuiltin> Sym,
                           uint32_t Offset, uint32_t Size);

  /// Return true; this item is always a virtual base pointer.
  /// \returns Always true.
  bool isVBPtr() const override { return true; }

private:
  std::unique_ptr<PDBSymbolTypeBuiltin> Type;
};

/// Layout item representing a data member of a UDT.
class DataMemberLayoutItem : public LayoutItemBase {
public:
  /// Construct a data-member layout item under \p Parent.
  /// \param Parent Containing UDT layout.
  /// \param DataMember PDB data symbol for the member.
  LLVM_ABI DataMemberLayoutItem(const UDTLayoutBase &Parent,
                                std::unique_ptr<PDBSymbolData> DataMember);

  /// Return the PDB data symbol for this member.
  /// \returns The PDB data symbol for this member.
  LLVM_ABI const PDBSymbolData &getDataMember();
  /// Return true if this member's type has a nested UDT layout.
  /// \returns True if this member's type has a nested UDT layout.
  LLVM_ABI bool hasUDTLayout() const;
  /// Return the nested class layout of this member's UDT type.
  /// \returns The nested class layout of this member's UDT type.
  LLVM_ABI const ClassLayout &getUDTLayout() const;

private:
  std::unique_ptr<PDBSymbolData> DataMember;
  std::unique_ptr<ClassLayout> UdtLayout;
};

/// Layout item representing a class's vtable pointer.
class VTableLayoutItem : public LayoutItemBase {
public:
  /// Construct a vtable layout item under \p Parent.
  /// \param Parent Containing UDT layout.
  /// \param VTable PDB vtable type symbol.
  LLVM_ABI VTableLayoutItem(const UDTLayoutBase &Parent,
                            std::unique_ptr<PDBSymbolTypeVTable> VTable);

  /// Return the size in bytes of each vtable element.
  /// \returns The size in bytes of each vtable element.
  uint32_t getElementSize() const { return ElementSize; }

private:
  uint32_t ElementSize = 0;
  std::unique_ptr<PDBSymbolTypeVTable> VTable;
};

/// Base class for layout information of a user-defined type (UDT).
class LLVM_ABI UDTLayoutBase : public LayoutItemBase {
  template <typename T> using UniquePtrVector = std::vector<std::unique_ptr<T>>;

public:
  /// Construct a UDT layout nested under \p Parent.
  /// \param Parent Containing UDT layout, or null for a top-level type.
  /// \param Sym PDB symbol for the UDT or base class.
  /// \param Name Display name of the type.
  /// \param OffsetInParent Byte offset within the parent, if nested.
  /// \param Size Size in bytes of the type.
  /// \param IsElided True if this UDT is omitted from physical layout.
  UDTLayoutBase(const UDTLayoutBase *Parent, const PDBSymbol &Sym,
                const std::string &Name, uint32_t OffsetInParent, uint32_t Size,
                bool IsElided);

  /// Deleted; UDT layouts are not copyable.
  /// \param Other Unused source layout.
  UDTLayoutBase(UDTLayoutBase const &Other) = delete;
  /// Deleted; UDT layouts are not assignable.
  /// \param Other Unused source layout.
  UDTLayoutBase &operator=(UDTLayoutBase const &Other) = delete;

  /// Return unused bytes at the end of this UDT, excluding child tail padding.
  /// \returns Unused trailing bytes, excluding child tail padding.
  uint32_t tailPadding() const override;
  /// Return the ordered list of non-elided layout children.
  /// \returns The ordered list of non-elided layout children.
  ArrayRef<LayoutItemBase *> layout_items() const { return LayoutItems; }
  /// Return all base-class layouts (non-virtual then virtual).
  /// \returns All base-class layouts (non-virtual then virtual).
  ArrayRef<BaseClassLayout *> bases() const { return AllBases; }
  /// Return the non-virtual base-class layouts.
  /// \returns The non-virtual base-class layouts.
  ArrayRef<BaseClassLayout *> regular_bases() const { return NonVirtualBases; }
  /// Return the virtual base-class layouts.
  /// \returns The virtual base-class layouts.
  ArrayRef<BaseClassLayout *> virtual_bases() const { return VirtualBases; }
  /// Return the number of direct virtual bases of this UDT.
  /// \returns The number of direct virtual bases of this UDT.
  uint32_t directVirtualBaseCount() const { return DirectVBaseCount; }
  /// Return the member functions belonging to this UDT.
  /// \returns The member functions belonging to this UDT.
  ArrayRef<std::unique_ptr<PDBSymbolFunc>> funcs() const { return Funcs; }
  /// Return child symbols that are neither bases, members, nor functions.
  /// \returns Child symbols that are neither bases, members, nor functions.
  ArrayRef<std::unique_ptr<PDBSymbol>> other_items() const { return Other; }

protected:
  /// Return true if a vbptr already exists at offset \p Off in this hierarchy.
  /// \param Off Offset within this UDT to query.
  /// \returns True if a vbptr already exists at \p Off.
  bool hasVBPtrAtOffset(uint32_t Off) const;
  /// Populate base, member, vtable, and function children from \p Sym.
  /// \param Sym PDB symbol whose children are enumerated.
  void initializeChildren(const PDBSymbol &Sym);

  /// Own \p Child and, if not elided, merge it into the layout bit vector.
  /// \param Child Layout item to insert under this UDT.
  void addChildToLayout(std::unique_ptr<LayoutItemBase> Child);

  /// Number of direct virtual bases of this UDT.
  uint32_t DirectVBaseCount = 0;

  /// Child symbols that are neither bases, data members, nor functions.
  UniquePtrVector<PDBSymbol> Other;
  /// Member functions belonging to this UDT.
  UniquePtrVector<PDBSymbolFunc> Funcs;
  /// Owning storage for all laid-out child items.
  UniquePtrVector<LayoutItemBase> ChildStorage;
  /// Non-elided layout children ordered by offset in the parent.
  std::vector<LayoutItemBase *> LayoutItems;

  /// All base-class layouts (non-virtual followed by virtual).
  std::vector<BaseClassLayout *> AllBases;
  /// View of the non-virtual bases within \c AllBases.
  ArrayRef<BaseClassLayout *> NonVirtualBases;
  /// View of the virtual bases within \c AllBases.
  ArrayRef<BaseClassLayout *> VirtualBases;

  /// Vtable layout item for this UDT, if any.
  VTableLayoutItem *VTable = nullptr;
  /// Virtual base pointer layout item for this UDT, if any.
  VBPtrLayoutItem *VBPtr = nullptr;
};

/// Layout of a base class subobject within a derived UDT.
class BaseClassLayout : public UDTLayoutBase {
public:
  /// Construct a base-class layout under \p Parent.
  /// \param Parent Containing derived-class layout.
  /// \param OffsetInParent Byte offset of the base within the parent.
  /// \param Elide True if the base should be omitted from physical layout.
  /// \param Base PDB base-class type symbol.
  LLVM_ABI BaseClassLayout(const UDTLayoutBase &Parent, uint32_t OffsetInParent,
                           bool Elide,
                           std::unique_ptr<PDBSymbolTypeBaseClass> Base);

  /// Return the PDB base-class type symbol.
  /// \returns The PDB base-class type symbol.
  const PDBSymbolTypeBaseClass &getBase() const { return *Base; }
  /// Return true if this is a virtual base class.
  /// \returns True if this is a virtual base class.
  bool isVirtualBase() const { return IsVirtualBase; }
  /// Return true if this is an empty base (size 1 with no laid-out storage).
  /// \returns True if this is an empty base (size 1 with no laid-out storage).
  bool isEmptyBase() { return SizeOf == 1 && LayoutSize == 0; }

private:
  std::unique_ptr<PDBSymbolTypeBaseClass> Base;
  bool IsVirtualBase;
};

/// Top-level layout of a class, struct, or union UDT.
class LLVM_ABI ClassLayout : public UDTLayoutBase {
public:
  /// Construct a class layout from the UDT symbol \p UDT.
  /// \param UDT PDB UDT type whose layout is computed.
  explicit ClassLayout(const PDBSymbolTypeUDT &UDT);
  /// Construct a class layout that takes ownership of \p UDT.
  /// \param UDT Owned PDB UDT type whose layout is computed.
  explicit ClassLayout(std::unique_ptr<PDBSymbolTypeUDT> UDT);

  /// Return the underlying PDB UDT type symbol.
  /// \returns The underlying PDB UDT type symbol.
  const PDBSymbolTypeUDT &getClass() const { return UDT; }
  /// Return padding bytes among this class's immediate children.
  /// \returns Padding bytes among this class's immediate children.
  uint32_t immediatePadding() const override;

private:
  BitVector ImmediateUsedBytes;
  std::unique_ptr<PDBSymbolTypeUDT> OwnedStorage;
  const PDBSymbolTypeUDT &UDT;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_UDTLAYOUT_H
