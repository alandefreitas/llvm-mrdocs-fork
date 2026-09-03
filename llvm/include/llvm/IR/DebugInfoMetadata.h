//===- llvm/IR/DebugInfoMetadata.h - Debug info metadata --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declarations for metadata specific to debug info.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DEBUGINFOMETADATA_H
#define LLVM_IR_DEBUGINFOMETADATA_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DbgVariableFragmentInfo.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PseudoProbe.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Discriminator.h"
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <type_traits>
#include <vector>

// Helper macros for defining get() overrides.
#define DEFINE_MDNODE_GET_UNPACK_IMPL(...) __VA_ARGS__
#define DEFINE_MDNODE_GET_UNPACK(ARGS) DEFINE_MDNODE_GET_UNPACK_IMPL ARGS
#define DEFINE_MDNODE_GET_DISTINCT_TEMPORARY(CLASS, FORMAL, ARGS)              \
  static CLASS *getDistinct(LLVMContext &Context,                              \
                            DEFINE_MDNODE_GET_UNPACK(FORMAL)) {                \
    return getImpl(Context, DEFINE_MDNODE_GET_UNPACK(ARGS), Distinct);         \
  }                                                                            \
  static Temp##CLASS getTemporary(LLVMContext &Context,                        \
                                  DEFINE_MDNODE_GET_UNPACK(FORMAL)) {          \
    return Temp##CLASS(                                                        \
        getImpl(Context, DEFINE_MDNODE_GET_UNPACK(ARGS), Temporary));          \
  }
#define DEFINE_MDNODE_GET(CLASS, FORMAL, ARGS)                                 \
  static CLASS *get(LLVMContext &Context, DEFINE_MDNODE_GET_UNPACK(FORMAL)) {  \
    return getImpl(Context, DEFINE_MDNODE_GET_UNPACK(ARGS), Uniqued);          \
  }                                                                            \
  static CLASS *getIfExists(LLVMContext &Context,                              \
                            DEFINE_MDNODE_GET_UNPACK(FORMAL)) {                \
    return getImpl(Context, DEFINE_MDNODE_GET_UNPACK(ARGS), Uniqued,           \
                   /* ShouldCreate */ false);                                  \
  }                                                                            \
  DEFINE_MDNODE_GET_DISTINCT_TEMPORARY(CLASS, FORMAL, ARGS)

namespace llvm {

namespace dwarf {
enum Tag : uint16_t;
}

/// Wrapper structure that holds source language identity metadata that includes
/// language name, optional language version, and an optional language dialect.
///
/// Some debug-info formats, particularly DWARF, distniguish between
/// language codes that include the version name and codes that don't.
/// DISourceLanguageName may hold either of these.
///
class DISourceLanguageName {
  /// Language version. The version scheme is language
  /// dependent.
  uint32_t Version = 0;

  /// Language name.
  /// If \ref HasVersion is \c true, then this name
  /// is version independent (i.e., doesn't include the language
  /// version in its name).
  uint16_t Name;

  /// If \c true, then \ref Version is interpretable and \ref Name
  /// is a version independent name.
  bool HasVersion;

  /// Optional target-specific language dialect for DWARF that can be used to
  /// indicate the programming/execution model.
  ///
  /// This is intentionally not modeled as a DICompileUnit operand. Code that
  /// introspects DICompileUnit through getNumOperands()/getOperand(i) will not
  /// see this field.
  uint16_t Dialect = 0;

public:
  /// Return true if this has versioned name.
  /// \return true if this has versioned name.
  bool hasVersionedName() const { return HasVersion; }

  /// Returns a versioned or unversioned language name.
  /// \return A versioned or unversioned language name.
  uint16_t getName() const { return Name; }

  /// Transitional API for cases where we do not yet support
  /// versioned source language names. Use \ref getName instead.
  ///
  /// FIXME: remove once all callers of this API account for versioned
  /// names.
  /// \return The unversioned language name.
  uint16_t getUnversionedName() const {
    assert(!hasVersionedName());
    return Name;
  }

  /// Returns language version. Only valid for versioned language names.
  /// \return Language version. Only valid for versioned language names.
  uint32_t getVersion() const {
    assert(hasVersionedName());
    return Version;
  }

  /// Return the dialect.
  /// \return The dialect.
  uint16_t getDialect() const { return Dialect; }

  /// Construct a versioned language name.
  /// \param Lang Language name code.
  /// \param Version Language version.
  /// \param Dialect Optional target-specific dialect.
  DISourceLanguageName(uint16_t Lang, uint32_t Version, uint16_t Dialect = 0)
      : Version(Version), Name(Lang), HasVersion(true), Dialect(Dialect) {}
  /// Construct an unversioned language name.
  /// \param Lang Language name code.
  /// \param Dialect Optional target-specific dialect.
  DISourceLanguageName(uint16_t Lang, uint16_t Dialect = 0)
      : Version(0), Name(Lang), HasVersion(false), Dialect(Dialect) {}
};

class DbgVariableRecord;

/// Command-line flag enabling flow-sensitive discriminators.
LLVM_ABI extern cl::opt<bool> EnableFSDiscriminator;

/// Tagged DWARF-like metadata node.
///
/// A metadata node with a DWARF tag (i.e., a constant named \c DW_TAG_*,
/// defined in llvm/BinaryFormat/Dwarf.h).  Called \a DINode because it's
/// potentially used for non-DWARF output.
///
/// Uses the SubclassData16 Metadata slot.
class DINode : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

protected:
  /// Construct a DINode.
  /// \param C LLVM context that owns the metadata.
  /// \param ID Metadata subclass ID.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Tag DWARF tag for the node.
  /// \param Ops1 First operand list.
  /// \param Ops2 Optional second operand list.
  DINode(LLVMContext &C, unsigned ID, StorageType Storage, unsigned Tag,
         ArrayRef<Metadata *> Ops1, ArrayRef<Metadata *> Ops2 = {})
      : MDNode(C, ID, Storage, Ops1, Ops2) {
    assert(Tag < 1u << 16);
    SubclassData16 = Tag;
  }
  /// Destroy this DINode.
  ~DINode() = default;

  /// Return the operand as.
  /// \param I Operand or argument index.
  /// \return The operand as.
  template <class Ty> Ty *getOperandAs(unsigned I) const {
    return cast_or_null<Ty>(getOperand(I));
  }

  /// Return the string operand.
  /// \param I Operand or argument index.
  /// \return The string operand.
  StringRef getStringOperand(unsigned I) const {
    if (auto *S = getOperandAs<MDString>(I))
      return S->getString();
    return StringRef();
  }

  /// Return the canonical md string.
  /// \param Context LLVM context that owns the metadata.
  /// \param S String to intern as MDString.
  /// \return The canonical md string.
  static MDString *getCanonicalMDString(LLVMContext &Context, StringRef S) {
    if (S.empty())
      return nullptr;
    return MDString::get(Context, S);
  }

  /// Allow subclasses to mutate the tag.
  /// \param Tag DWARF tag for the node.
  void setTag(unsigned Tag) { SubclassData16 = Tag; }

public:
  /// Return the tag.
  /// \return The tag.
  LLVM_ABI dwarf::Tag getTag() const;

  /// Debug info flags.
  ///
  /// The three accessibility flags are mutually exclusive and rolled together
  /// in the first two bits.
  enum DIFlags : uint32_t {
#define HANDLE_DI_FLAG(ID, NAME) Flag##NAME = ID,
#define DI_FLAG_LARGEST_NEEDED
#include "llvm/IR/DebugInfoFlags.def"
    /// Mask of FlagPrivate | FlagProtected | FlagPublic.
    FlagAccessibility = FlagPrivate | FlagProtected | FlagPublic,
    /// Mask of single/multiple/virtual inheritance pointer-to-member reps.
    FlagPtrToMemberRep = FlagSingleInheritance | FlagMultipleInheritance |
                         FlagVirtualInheritance,
    /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
    LLVM_MARK_AS_BITMASK_ENUM(FlagLargest)
  };

  /// Return the flag.
  /// \param Flag Flag enumerator or flag name string.
  /// \return The flag.
  LLVM_ABI static DIFlags getFlag(StringRef Flag);
  /// Return the flag string.
  /// \param Flag Flag enumerator or flag name string.
  /// \return The flag string.
  LLVM_ABI static StringRef getFlagString(DIFlags Flag);

  /// Split up a flags bitfield.
  ///
  /// Split \c Flags into \c SplitFlags, a vector of its components.  Returns
  /// any remaining (unrecognized) bits.
  /// \param Flags Flags bitfield.
  /// \param SplitFlags Receives individual flag components.
  /// \return Any remaining (unrecognized) bits.
  LLVM_ABI static DIFlags splitFlags(DIFlags Flags,
                                     SmallVectorImpl<DIFlags> &SplitFlags);

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    switch (MD->getMetadataID()) {
    default:
      return false;
    case GenericDINodeKind:
    case DISubrangeKind:
    case DIEnumeratorKind:
    case DIBasicTypeKind:
    case DIFixedPointTypeKind:
    case DIStringTypeKind:
    case DISubrangeTypeKind:
    case DIDerivedTypeKind:
    case DICompositeTypeKind:
    case DISubroutineTypeKind:
    case DIFileKind:
    case DICompileUnitKind:
    case DISubprogramKind:
    case DILexicalBlockKind:
    case DILexicalBlockFileKind:
    case DINamespaceKind:
    case DICommonBlockKind:
    case DITemplateTypeParameterKind:
    case DITemplateValueParameterKind:
    case DIGlobalVariableKind:
    case DILocalVariableKind:
    case DILabelKind:
    case DIObjCPropertyKind:
    case DIPropertyKind:
    case DIImportedEntityKind:
    case DIModuleKind:
    case DIGenericSubrangeKind:
    case DIAssignIDKind:
      return true;
    }
  }
};

/// Generic tagged DWARF-like metadata node.
///
/// An un-specialized DWARF-like metadata node.  The first operand is a
/// (possibly empty) null-separated \a MDString header that contains arbitrary
/// fields.  The remaining operands are \a dwarf_operands(), and are pointers
/// to other metadata.
///
/// Uses the SubclassData32 Metadata slot.
class GenericDINode : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  GenericDINode(LLVMContext &C, StorageType Storage, unsigned Hash,
                unsigned Tag, ArrayRef<Metadata *> Ops1,
                ArrayRef<Metadata *> Ops2)
      : DINode(C, GenericDINodeKind, Storage, Tag, Ops1, Ops2) {
    /// Set the hash.
    /// \param Hash Precomputed hash value.
    setHash(Hash);
  }
  /// Destroy this GenericDINode.
  ~GenericDINode() { dropAllReferences(); }

  /// Set the hash.
  /// \param Hash Precomputed hash value.
  void setHash(unsigned Hash) { SubclassData32 = Hash; }
  /// Recalculate the cached hash value.
  void recalculateHash();

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Header Null-separated header string.
  /// \param DwarfOps DWARF operand metadata nodes.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static GenericDINode *getImpl(LLVMContext &Context, unsigned Tag,
                                StringRef Header, ArrayRef<Metadata *> DwarfOps,
                                StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Tag, getCanonicalMDString(Context, Header),
                   DwarfOps, Storage, ShouldCreate);
  }

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Header Null-separated header string.
  /// \param DwarfOps DWARF operand metadata nodes.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static GenericDINode *getImpl(LLVMContext &Context, unsigned Tag,
                                         MDString *Header,
                                         ArrayRef<Metadata *> DwarfOps,
                                         StorageType Storage,
                                         bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempGenericDINode cloneImpl() const {
    return getTemporary(getContext(), getTag(), getHeader(),
                        SmallVector<Metadata *, 4>(dwarf_operands()));
  }

public:
  /// Return the hash.
  /// \return The hash.
  unsigned getHash() const { return SubclassData32; }

  /// Get or create a GenericDINode with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Header Null-separated header string.
  /// \param DwarfOps DWARF operand metadata nodes.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(GenericDINode,
                    (unsigned Tag, StringRef Header,
                     ArrayRef<Metadata *> DwarfOps),
                    (Tag, Header, DwarfOps))
  /// Get or create a GenericDINode with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Header Null-separated header string.
  /// \param DwarfOps DWARF operand metadata nodes.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(GenericDINode,
                    (unsigned Tag, MDString *Header,
                     ArrayRef<Metadata *> DwarfOps),
                    (Tag, Header, DwarfOps))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempGenericDINode clone() const { return cloneImpl(); }

  /// Return the tag.
  /// \return The tag.
  LLVM_ABI dwarf::Tag getTag() const;
  /// Return the header.
  /// \return The header.
  StringRef getHeader() const { return getStringOperand(0); }
  /// Return the raw header operand.
  /// \return The raw header operand.
  MDString *getRawHeader() const { return getOperandAs<MDString>(0); }

  /// Return the dwarf op begin.
  /// \return The dwarf op begin.
  op_iterator dwarf_op_begin() const { return op_begin() + 1; }
  /// Return the dwarf op end.
  /// \return The dwarf op end.
  op_iterator dwarf_op_end() const { return op_end(); }
  /// Return the dwarf operands.
  /// \return The dwarf operands.
  op_range dwarf_operands() const {
    return op_range(dwarf_op_begin(), dwarf_op_end());
  }

  /// Return the num dwarf operands.
  /// \return The num dwarf operands.
  unsigned getNumDwarfOperands() const { return getNumOperands() - 1; }
  /// Return the dwarf operand.
  /// \param I Operand or argument index.
  /// \return The dwarf operand.
  const MDOperand &getDwarfOperand(unsigned I) const {
    return getOperand(I + 1);
  }
  /// Replace the dwarf operand with.
  /// \param I Operand or argument index.
  /// \param New Replacement metadata value.
  void replaceDwarfOperandWith(unsigned I, Metadata *New) {
    /// Replace the operand with.
    /// \param New Replacement metadata value.
    replaceOperandWith(I + 1, New);
  }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == GenericDINodeKind;
  }
};

/// Assignment ID linking stores to dbg.assign intrinsics.
///
/// Used to link stores (as an attachment) and dbg.assigns (as an operand).
/// DIAssignID metadata is never uniqued as we compare instances using
/// referential equality (the instance/address is the ID).
class DIAssignID : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  DIAssignID(LLVMContext &C, StorageType Storage)
      : MDNode(C, DIAssignIDKind, Storage, {}) {}

  /// Destroy this DIAssignID.
  ~DIAssignID() { dropAllReferences(); }

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIAssignID *getImpl(LLVMContext &Context, StorageType Storage,
                                      bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIAssignID cloneImpl() const { return getTemporary(getContext()); }

public:
  // This node has no operands to replace.
  /// Replace the operand with.
  /// \param I Operand or argument index.
  /// \param New Replacement metadata value.
  void replaceOperandWith(unsigned I, Metadata *New) = delete;

  /// Return the all dbg variable record users.
  /// \return All DbgVariableRecord users of this metadata.
  SmallVector<DbgVariableRecord *> getAllDbgVariableRecordUsers() {
    return Context.getReplaceableUses()->getAllDbgVariableRecordUsers();
  }

  /// Get a distinct DIAssignID for \p Context.
  /// \param Context LLVM context that owns the metadata.
  /// \return A distinct DIAssignID instance.
  static DIAssignID *getDistinct(LLVMContext &Context) {
    return getImpl(Context, Distinct);
  }
  /// Get a temporary DIAssignID for \p Context.
  /// \param Context LLVM context that owns the metadata.
  /// \return A temporary DIAssignID instance.
  static TempDIAssignID getTemporary(LLVMContext &Context) {
    return TempDIAssignID(getImpl(Context, Temporary));
  }
  // NOTE: Do not define get(LLVMContext&) - see class comment.

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIAssignIDKind;
  }
};

/// Array subrange.
class DISubrange : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  /// DI Subrange.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Ops Operand list, or expression opcodes.
  DISubrange(LLVMContext &C, StorageType Storage, ArrayRef<Metadata *> Ops);

  /// Destroy this DISubrange.
  ~DISubrange() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Count Element count for a subrange.
  /// \param LowerBound Lower bound of a subrange.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DISubrange *getImpl(LLVMContext &Context, int64_t Count,
                                      int64_t LowerBound, StorageType Storage,
                                      bool ShouldCreate = true);

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param CountNode Metadata node describing the element count.
  /// \param LowerBound Lower bound of a subrange.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DISubrange *getImpl(LLVMContext &Context, Metadata *CountNode,
                                      int64_t LowerBound, StorageType Storage,
                                      bool ShouldCreate = true);

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param CountNode Metadata node describing the element count.
  /// \param LowerBound Lower bound of a subrange.
  /// \param UpperBound Upper bound of a subrange.
  /// \param Stride Stride of a subrange.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DISubrange *getImpl(LLVMContext &Context, Metadata *CountNode,
                                      Metadata *LowerBound,
                                      Metadata *UpperBound, Metadata *Stride,
                                      StorageType Storage,
                                      bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDISubrange cloneImpl() const {
    return getTemporary(getContext(), getRawCountNode(), getRawLowerBound(),
                        getRawUpperBound(), getRawStride());
  }

public:
  /// Get or create a DISubrange with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Count Element count for a subrange.
  /// \param LowerBound Lower bound of a subrange.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DISubrange, (int64_t Count, int64_t LowerBound = 0),
                    (Count, LowerBound))

  /// Get or create a DISubrange with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param CountNode Metadata node describing the element count.
  /// \param LowerBound Lower bound of a subrange.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DISubrange, (Metadata * CountNode, int64_t LowerBound = 0),
                    (CountNode, LowerBound))

  /// Get or create a DISubrange with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param CountNode Metadata node describing the element count.
  /// \param LowerBound Lower bound of a subrange.
  /// \param UpperBound Upper bound of a subrange.
  /// \param Stride Stride of a subrange.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DISubrange,
                    (Metadata * CountNode, Metadata *LowerBound,
                     Metadata *UpperBound, Metadata *Stride),
                    (CountNode, LowerBound, UpperBound, Stride))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDISubrange clone() const { return cloneImpl(); }

  /// Return the raw count node operand.
  /// \return The raw count node operand.
  Metadata *getRawCountNode() const { return getOperand(0).get(); }

  /// Return the raw lower bound operand.
  /// \return The raw lower bound operand.
  Metadata *getRawLowerBound() const { return getOperand(1).get(); }

  /// Return the raw upper bound operand.
  /// \return The raw upper bound operand.
  Metadata *getRawUpperBound() const { return getOperand(2).get(); }

  /// Return the raw stride operand.
  /// \return The raw stride operand.
  Metadata *getRawStride() const { return getOperand(3).get(); }

  /// Pointer-union type for a subrange bound.
  typedef PointerUnion<ConstantInt *, DIVariable *, DIExpression *> BoundType;

  /// Return the count.
  /// \return The count.
  LLVM_ABI BoundType getCount() const;

  /// Return the lower bound.
  /// \return The lower bound.
  LLVM_ABI BoundType getLowerBound() const;

  /// Return the upper bound.
  /// \return The upper bound.
  LLVM_ABI BoundType getUpperBound() const;

  /// Return the stride.
  /// \return The stride.
  LLVM_ABI BoundType getStride() const;

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DISubrangeKind;
  }
};

/// Generic array subrange with metadata-valued bounds.
class DIGenericSubrange : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  /// DI Generic Subrange.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Ops Operand list, or expression opcodes.
  DIGenericSubrange(LLVMContext &C, StorageType Storage,
                    ArrayRef<Metadata *> Ops);

  /// Destroy this DIGenericSubrange.
  ~DIGenericSubrange() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param CountNode Metadata node describing the element count.
  /// \param LowerBound Lower bound of a subrange.
  /// \param UpperBound Upper bound of a subrange.
  /// \param Stride Stride of a subrange.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIGenericSubrange *
  getImpl(LLVMContext &Context, Metadata *CountNode, Metadata *LowerBound,
          Metadata *UpperBound, Metadata *Stride, StorageType Storage,
          bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIGenericSubrange cloneImpl() const {
    return getTemporary(getContext(), getRawCountNode(), getRawLowerBound(),
                        getRawUpperBound(), getRawStride());
  }

public:
  /// Get or create a DIGenericSubrange with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param CountNode Metadata node describing the element count.
  /// \param LowerBound Lower bound of a subrange.
  /// \param UpperBound Upper bound of a subrange.
  /// \param Stride Stride of a subrange.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIGenericSubrange,
                    (Metadata * CountNode, Metadata *LowerBound,
                     Metadata *UpperBound, Metadata *Stride),
                    (CountNode, LowerBound, UpperBound, Stride))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIGenericSubrange clone() const { return cloneImpl(); }

  /// Return the raw count node operand.
  /// \return The raw count node operand.
  Metadata *getRawCountNode() const { return getOperand(0).get(); }
  /// Return the raw lower bound operand.
  /// \return The raw lower bound operand.
  Metadata *getRawLowerBound() const { return getOperand(1).get(); }
  /// Return the raw upper bound operand.
  /// \return The raw upper bound operand.
  Metadata *getRawUpperBound() const { return getOperand(2).get(); }
  /// Return the raw stride operand.
  /// \return The raw stride operand.
  Metadata *getRawStride() const { return getOperand(3).get(); }

  /// Pointer-union type for a subrange bound.
  using BoundType = PointerUnion<DIVariable *, DIExpression *>;

  /// Return the count.
  /// \return The count.
  LLVM_ABI BoundType getCount() const;
  /// Return the lower bound.
  /// \return The lower bound.
  LLVM_ABI BoundType getLowerBound() const;
  /// Return the upper bound.
  /// \return The upper bound.
  LLVM_ABI BoundType getUpperBound() const;
  /// Return the stride.
  /// \return The stride.
  LLVM_ABI BoundType getStride() const;

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIGenericSubrangeKind;
  }
};

/// Enumeration value.
///
/// TODO: Add a pointer to the context (DW_TAG_enumeration_type) once that no
/// longer creates a type cycle.
class DIEnumerator : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  /// DI Enumerator.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Value Enumerator or template value.
  /// \param IsUnsigned Whether the enumerator is unsigned.
  /// \param Ops Operand list, or expression opcodes.
  APInt Value;
  LLVM_ABI DIEnumerator(LLVMContext &C, StorageType Storage, const APInt &Value,
                        bool IsUnsigned, ArrayRef<Metadata *> Ops);
  DIEnumerator(LLVMContext &C, StorageType Storage, int64_t Value,
               bool IsUnsigned, ArrayRef<Metadata *> Ops)
      : DIEnumerator(C, Storage, APInt(64, Value, !IsUnsigned), IsUnsigned,
                     Ops) {}
  /// Destroy this DIEnumerator.
  ~DIEnumerator() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Value Enumerator or template value.
  /// \param IsUnsigned Whether the enumerator is unsigned.
  /// \param Name Source-level name.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIEnumerator *getImpl(LLVMContext &Context, const APInt &Value,
                               bool IsUnsigned, StringRef Name,
                               StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Value, IsUnsigned,
                   getCanonicalMDString(Context, Name), Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Value Enumerator or template value.
  /// \param IsUnsigned Whether the enumerator is unsigned.
  /// \param Name Source-level name.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIEnumerator *getImpl(LLVMContext &Context,
                                        const APInt &Value, bool IsUnsigned,
                                        MDString *Name, StorageType Storage,
                                        bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIEnumerator cloneImpl() const {
    return getTemporary(getContext(), getValue(), isUnsigned(), getName());
  }

public:
  /// Get or create a DIEnumerator with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Value Enumerator or template value.
  /// \param IsUnsigned Whether the enumerator is unsigned.
  /// \param Name Source-level name.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIEnumerator,
                    (int64_t Value, bool IsUnsigned, StringRef Name),
                    (APInt(64, Value, !IsUnsigned), IsUnsigned, Name))
  /// Get or create a DIEnumerator with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Value Enumerator or template value.
  /// \param IsUnsigned Whether the enumerator is unsigned.
  /// \param Name Source-level name.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIEnumerator,
                    (int64_t Value, bool IsUnsigned, MDString *Name),
                    (APInt(64, Value, !IsUnsigned), IsUnsigned, Name))
  /// Get or create a DIEnumerator with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Value Enumerator or template value.
  /// \param IsUnsigned Whether the enumerator is unsigned.
  /// \param Name Source-level name.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIEnumerator,
                    (APInt Value, bool IsUnsigned, StringRef Name),
                    (Value, IsUnsigned, Name))
  /// Get or create a DIEnumerator with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Value Enumerator or template value.
  /// \param IsUnsigned Whether the enumerator is unsigned.
  /// \param Name Source-level name.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIEnumerator,
                    (APInt Value, bool IsUnsigned, MDString *Name),
                    (Value, IsUnsigned, Name))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIEnumerator clone() const { return cloneImpl(); }

  /// Return the value.
  /// \return The value.
  const APInt &getValue() const { return Value; }
  /// Return true if this is unsigned.
  /// \return true if this is unsigned.
  bool isUnsigned() const { return SubclassData32; }
  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(0); }

  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(0); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIEnumeratorKind;
  }
};

/// Base class for scope-like contexts.
///
/// Base class for lexical scopes and types (which are also declaration
/// contexts).
///
/// TODO: Separate the concepts of declaration contexts and lexical scopes.
class DIScope : public DINode {
protected:
  /// Construct a DIScope.
  /// \param C LLVM context that owns the metadata.
  /// \param ID Metadata subclass ID.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Tag DWARF tag for the node.
  /// \param Ops Operand list for the scope.
  DIScope(LLVMContext &C, unsigned ID, StorageType Storage, unsigned Tag,
          ArrayRef<Metadata *> Ops)
      : DINode(C, ID, Storage, Tag, Ops) {}
  /// Destroy this DIScope.
  ~DIScope() = default;

public:
  /// Return the file.
  /// \return The file.
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }

  /// Return the filename.
  /// \return The filename.
  inline StringRef getFilename() const;
  /// Return the directory.
  /// \return The directory.
  inline StringRef getDirectory() const;
  /// Return the source.
  /// \return The source.
  inline std::optional<StringRef> getSource() const;

  /// Return the name.
  /// \return The name.
  LLVM_ABI StringRef getName() const;
  /// Return the scope.
  /// \return The scope.
  LLVM_ABI DIScope *getScope() const;

  /// Return the raw underlying file.
  ///
  /// A \a DIFile is a \a DIScope, but it doesn't point at a separate file (it
  /// \em is the file).  If \c this is an \a DIFile, we need to return \c this.
  /// Otherwise, return the first operand, which is where all other subclasses
  /// store their file pointer.
  /// \return The raw underlying file.
  Metadata *getRawFile() const {
    return isa<DIFile>(this) ? const_cast<DIScope *>(this)
                             : static_cast<Metadata *>(getOperand(0));
  }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    switch (MD->getMetadataID()) {
    default:
      return false;
    case DIBasicTypeKind:
    case DIFixedPointTypeKind:
    case DIStringTypeKind:
    case DISubrangeTypeKind:
    case DIDerivedTypeKind:
    case DICompositeTypeKind:
    case DISubroutineTypeKind:
    case DIFileKind:
    case DICompileUnitKind:
    case DISubprogramKind:
    case DILexicalBlockKind:
    case DILexicalBlockFileKind:
    case DINamespaceKind:
    case DICommonBlockKind:
    case DIModuleKind:
      return true;
    }
  }
};

/// File.
///
/// TODO: Merge with directory/file node (including users).
/// TODO: Canonicalize paths on creation.
class DIFile : public DIScope {
  friend class LLVMContextImpl;
  friend class MDNode;

public:
  /// Which algorithm (e.g. MD5) a checksum was generated with.
  ///
  /// The encoding is explicit because it is used directly in Bitcode. The
  /// value 0 is reserved to indicate the absence of a checksum in Bitcode.
  enum ChecksumKind {
    // The first variant was originally CSK_None, encoded as 0. The new
    // internal representation removes the need for this by wrapping the
    // ChecksumInfo in an Optional, but to preserve Bitcode compatibility the 0
    // encoding is reserved.
    /// MD5 checksum of the file contents.
    CSK_MD5 = 1,
    /// SHA-1 checksum of the file contents.
    CSK_SHA1 = 2,
    /// SHA-256 checksum of the file contents.
    CSK_SHA256 = 3,
    /// Last valid checksum kind (equal to CSK_SHA256).
    CSK_Last = CSK_SHA256 // Should be last enumeration.
  };

  /// A single checksum, represented by a \a Kind and a \a Value (a string).
  template <typename T> struct ChecksumInfo {
    /// The kind of checksum which \a Value encodes.
    ChecksumKind Kind;
    /// The string value of the checksum.
    T Value;

    /// Construct a checksum kind/value pair.
    /// \param Kind Checksum algorithm kind.
    /// \param Value Checksum string value.
    ChecksumInfo(ChecksumKind Kind, T Value) : Kind(Kind), Value(Value) {}
    /// Destroy this ChecksumInfo.
    ~ChecksumInfo() = default;
    /// Return true if the two values compare equal.
    /// \param X Other checksum info to compare against.
    /// \return true if the two values compare equal.
    bool operator==(const ChecksumInfo<T> &X) const {
      return Kind == X.Kind && Value == X.Value;
    }
    /// Return true if the two values compare unequal.
    /// \param X Other checksum info to compare against.
    /// \return true if the two values compare unequal.
    bool operator!=(const ChecksumInfo<T> &X) const { return !(*this == X); }
    /// Return the kind as string.
    /// \return The kind as string.
    StringRef getKindAsString() const { return getChecksumKindAsString(Kind); }
  };

private:
  std::optional<ChecksumInfo<MDString *>> Checksum;
  /// An optional source. A nullptr means none.
  MDString *Source;

  /// DI File.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param CS The cs.
  /// \param Src The src.
  /// \param Ops Operand list, or expression opcodes.
  DIFile(LLVMContext &C, StorageType Storage,
         std::optional<ChecksumInfo<MDString *>> CS, MDString *Src,
         ArrayRef<Metadata *> Ops);
  /// Destroy this DIFile.
  ~DIFile() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Filename Source filename.
  /// \param Directory Source directory.
  /// \param CS The cs.
  /// \param Source Optional embedded source text.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIFile *getImpl(LLVMContext &Context, StringRef Filename,
                         StringRef Directory,
                         std::optional<ChecksumInfo<StringRef>> CS,
                         std::optional<StringRef> Source, StorageType Storage,
                         bool ShouldCreate = true) {
    std::optional<ChecksumInfo<MDString *>> MDChecksum;
    if (CS)
      MDChecksum.emplace(CS->Kind, getCanonicalMDString(Context, CS->Value));
    return getImpl(Context, getCanonicalMDString(Context, Filename),
                   getCanonicalMDString(Context, Directory), MDChecksum,
                   Source ? MDString::get(Context, *Source) : nullptr, Storage,
                   ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Filename Source filename.
  /// \param Directory Source directory.
  /// \param CS The cs.
  /// \param Source Optional embedded source text.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIFile *getImpl(LLVMContext &Context, MDString *Filename,
                                  MDString *Directory,
                                  std::optional<ChecksumInfo<MDString *>> CS,
                                  MDString *Source, StorageType Storage,
                                  bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIFile cloneImpl() const {
    return getTemporary(getContext(), getFilename(), getDirectory(),
                        getChecksum(), getSource());
  }

public:
  /// Get or create a DIFile with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Filename Source filename.
  /// \param Directory Source directory.
  /// \param CS Optional file checksum.
  /// \param Source Optional embedded source text.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIFile,
                    (StringRef Filename, StringRef Directory,
                     std::optional<ChecksumInfo<StringRef>> CS = std::nullopt,
                     std::optional<StringRef> Source = std::nullopt),
                    (Filename, Directory, CS, Source))
  /// Get or create a DIFile with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Filename Source filename.
  /// \param Directory Source directory.
  /// \param CS Optional file checksum.
  /// \param Source Optional embedded source text.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIFile,
                    (MDString * Filename, MDString *Directory,
                     std::optional<ChecksumInfo<MDString *>> CS = std::nullopt,
                     MDString *Source = nullptr),
                    (Filename, Directory, CS, Source))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIFile clone() const { return cloneImpl(); }

  /// Return the filename.
  /// \return The filename.
  StringRef getFilename() const { return getStringOperand(0); }
  /// Return the directory.
  /// \return The directory.
  StringRef getDirectory() const { return getStringOperand(1); }
  /// Return the checksum.
  /// \return The checksum.
  std::optional<ChecksumInfo<StringRef>> getChecksum() const {
    std::optional<ChecksumInfo<StringRef>> StringRefChecksum;
    if (Checksum)
      StringRefChecksum.emplace(Checksum->Kind, Checksum->Value->getString());
    return StringRefChecksum;
  }
  /// Return the source.
  /// \return The source.
  std::optional<StringRef> getSource() const {
    return Source ? std::optional<StringRef>(Source->getString())
                  : std::nullopt;
  }

  /// Return the raw filename operand.
  /// \return The raw filename operand.
  MDString *getRawFilename() const { return getOperandAs<MDString>(0); }
  /// Return the raw directory operand.
  /// \return The raw directory operand.
  MDString *getRawDirectory() const { return getOperandAs<MDString>(1); }
  /// Return the raw checksum operand.
  /// \return The raw checksum operand.
  std::optional<ChecksumInfo<MDString *>> getRawChecksum() const {
    return Checksum;
  }
  /// Return the raw source operand.
  /// \return The raw source operand.
  MDString *getRawSource() const { return Source; }

  /// Return the checksum kind as string.
  /// \param CSKind The cs kind.
  /// \return The checksum kind as string.
  LLVM_ABI static StringRef getChecksumKindAsString(ChecksumKind CSKind);
  /// Return the checksum kind.
  /// \param CSKindStr The cs kind str.
  /// \return The checksum kind.
  LLVM_ABI static std::optional<ChecksumKind>
  getChecksumKind(StringRef CSKindStr);

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIFileKind;
  }
};

/// Return the filename.
/// \return The filename.
StringRef DIScope::getFilename() const {
  if (auto *F = getFile())
    return F->getFilename();
  return "";
}

/// Return the directory.
/// \return The directory.
StringRef DIScope::getDirectory() const {
  if (auto *F = getFile())
    return F->getDirectory();
  return "";
}

/// Return the source.
/// \return The source.
std::optional<StringRef> DIScope::getSource() const {
  if (auto *F = getFile())
    return F->getSource();
  return std::nullopt;
}

/// Base class for types.
///
/// TODO: Remove the hardcoded name and context, since many types don't use
/// them.
/// TODO: Split up flags.
///
/// Uses the SubclassData32 Metadata slot.
class DIType : public DIScope {
  unsigned Line;
  DIFlags Flags;
  uint32_t NumExtraInhabitants;

protected:
  /// Number of fixed operands shared by all DIType subclasses.
  static constexpr unsigned N_OPERANDS = 5;

  /// Construct a DIType.
  /// \param C LLVM context that owns the metadata.
  /// \param ID Metadata subclass ID.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Tag DWARF tag for the node.
  /// \param Line Source line number.
  /// \param AlignInBits ABI alignment in bits.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param Flags Flags bitfield.
  /// \param Ops Operand list for the type.
  DIType(LLVMContext &C, unsigned ID, StorageType Storage, unsigned Tag,
         unsigned Line, uint32_t AlignInBits, uint32_t NumExtraInhabitants,
         DIFlags Flags, ArrayRef<Metadata *> Ops)
      : DIScope(C, ID, Storage, Tag, Ops) {
    init(Line, AlignInBits, NumExtraInhabitants, Flags);
  }
  /// Destroy this DIType.
  ~DIType() = default;

  /// Initialize common DIType fields.
  /// \param Line Source line number.
  /// \param AlignInBits ABI alignment in bits.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param Flags Flags bitfield.
  void init(unsigned Line, uint32_t AlignInBits, uint32_t NumExtraInhabitants,
            DIFlags Flags) {
    this->Line = Line;
    this->Flags = Flags;
    this->SubclassData32 = AlignInBits;
    this->NumExtraInhabitants = NumExtraInhabitants;
  }

  /// Change fields in place.
  /// \param Tag DWARF tag for the node.
  /// \param Line Source line number.
  /// \param AlignInBits ABI alignment in bits.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param Flags Flags bitfield.
  void mutate(unsigned Tag, unsigned Line, uint32_t AlignInBits,
              uint32_t NumExtraInhabitants, DIFlags Flags) {
    assert(isDistinct() && "Only distinct nodes can mutate");
    /// Set the tag.
    /// \param Tag DWARF tag for the node.
    setTag(Tag);
    /// Initialize common DIType fields.
    /// \param Line Source line number.
    /// \param AlignInBits ABI alignment in bits.
    /// \param NumExtraInhabitants Number of extra inhabitants for the type.
    /// \param Flags Flags bitfield.
    init(Line, AlignInBits, NumExtraInhabitants, Flags);
  }

public:
  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIType clone() const {
    return TempDIType(cast<DIType>(MDNode::clone().release()));
  }

  /// Return the line.
  /// \return The line.
  unsigned getLine() const { return Line; }
  /// Return the align in bits.
  /// \return The align in bits.
  LLVM_ABI uint32_t getAlignInBits() const;
  /// Return the align in bytes.
  /// \return The align in bytes.
  uint32_t getAlignInBytes() const { return getAlignInBits() / CHAR_BIT; }
  /// Return the num extra inhabitants.
  /// \return The num extra inhabitants.
  uint32_t getNumExtraInhabitants() const { return NumExtraInhabitants; }
  /// Return the flags.
  /// \return The flags.
  DIFlags getFlags() const { return Flags; }

  /// Return the scope.
  /// \return The scope.
  DIScope *getScope() const { return cast_or_null<DIScope>(getRawScope()); }
  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(2); }

  /// Return the raw scope operand.
  /// \return The raw scope operand.
  Metadata *getRawScope() const { return getOperand(1); }
  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(2); }

  /// Return the raw size in bits operand.
  /// \return The raw size in bits operand.
  Metadata *getRawSizeInBits() const { return getOperand(3); }
  /// Return the size in bits.
  /// \return The size in bits.
  uint64_t getSizeInBits() const {
    if (auto *MD = dyn_cast_or_null<ConstantAsMetadata>(getRawSizeInBits())) {
      if (ConstantInt *CI = dyn_cast_or_null<ConstantInt>(MD->getValue()))
        return CI->getZExtValue();
    }
    return 0;
  }

  /// Return the raw offset in bits operand.
  /// \return The raw offset in bits operand.
  Metadata *getRawOffsetInBits() const { return getOperand(4); }
  /// Return the offset in bits.
  /// \return The offset in bits.
  uint64_t getOffsetInBits() const {
    if (auto *MD = dyn_cast_or_null<ConstantAsMetadata>(getRawOffsetInBits())) {
      if (ConstantInt *CI = dyn_cast_or_null<ConstantInt>(MD->getValue()))
        return CI->getZExtValue();
    }
    return 0;
  }

  /// Returns a new temporary DIType with updated Flags
  /// \param NewFlags Replacement DIFlags value.
  /// \return A new temporary DIType with updated Flags.
  TempDIType cloneWithFlags(DIFlags NewFlags) const {
    auto NewTy = clone();
    NewTy->Flags = NewFlags;
    return NewTy;
  }

  /// Return true if this is private.
  /// \return true if this is private.
  bool isPrivate() const {
    return (getFlags() & FlagAccessibility) == FlagPrivate;
  }
  /// Return true if this is protected.
  /// \return true if this is protected.
  bool isProtected() const {
    return (getFlags() & FlagAccessibility) == FlagProtected;
  }
  /// Return true if this is public.
  /// \return true if this is public.
  bool isPublic() const {
    return (getFlags() & FlagAccessibility) == FlagPublic;
  }
  /// Return true if this is forward decl.
  /// \return true if this is forward decl.
  bool isForwardDecl() const { return getFlags() & FlagFwdDecl; }
  /// Return true if this is apple block extension.
  /// \return true if this is apple block extension.
  bool isAppleBlockExtension() const { return getFlags() & FlagAppleBlock; }
  /// Return true if this is virtual.
  /// \return true if this is virtual.
  bool isVirtual() const { return getFlags() & FlagVirtual; }
  /// Return true if this is artificial.
  /// \return true if this is artificial.
  bool isArtificial() const { return getFlags() & FlagArtificial; }
  /// Return true if this is object pointer.
  /// \return true if this is object pointer.
  bool isObjectPointer() const { return getFlags() & FlagObjectPointer; }
  /// Return true if this is objc class complete.
  /// \return true if this is objc class complete.
  bool isObjcClassComplete() const {
    return getFlags() & FlagObjcClassComplete;
  }
  /// Return true if this is vector.
  /// \return true if this is vector.
  bool isVector() const { return getFlags() & FlagVector; }
  /// Return true if this is bit field.
  /// \return true if this is bit field.
  bool isBitField() const { return getFlags() & FlagBitField; }
  /// Return true if this is static member.
  /// \return true if this is static member.
  bool isStaticMember() const { return getFlags() & FlagStaticMember; }
  /// Return true if this is l value reference.
  /// \return true if this is l value reference.
  bool isLValueReference() const { return getFlags() & FlagLValueReference; }
  /// Return true if this is r value reference.
  /// \return true if this is r value reference.
  bool isRValueReference() const { return getFlags() & FlagRValueReference; }
  /// Return true if this is type pass by value.
  /// \return true if this is type pass by value.
  bool isTypePassByValue() const { return getFlags() & FlagTypePassByValue; }
  /// Return true if this is type pass by reference.
  /// \return true if this is type pass by reference.
  bool isTypePassByReference() const {
    return getFlags() & FlagTypePassByReference;
  }
  /// Return true if this is big endian.
  /// \return true if this is big endian.
  bool isBigEndian() const { return getFlags() & FlagBigEndian; }
  /// Return true if this is little endian.
  /// \return true if this is little endian.
  bool isLittleEndian() const { return getFlags() & FlagLittleEndian; }
  /// Return the export symbols.
  /// \return The export symbols.
  bool getExportSymbols() const { return getFlags() & FlagExportSymbols; }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    switch (MD->getMetadataID()) {
    default:
      return false;
    case DIBasicTypeKind:
    case DIFixedPointTypeKind:
    case DIStringTypeKind:
    case DISubrangeTypeKind:
    case DIDerivedTypeKind:
    case DICompositeTypeKind:
    case DISubroutineTypeKind:
      return true;
    }
  }
};

/// Basic type, like 'int' or 'float'.
///
/// TODO: Split out DW_TAG_unspecified_type.
/// TODO: Drop unused accessors.
class DIBasicType : public DIType {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Encoding;
  /// Describes the number of bits used by the value of the object. Non-zero
  /// when the value of an object does not fully occupy the storage size
  /// specified by SizeInBits.
  uint32_t DataSizeInBits;

protected:
  /// Construct a DIBasicType.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Tag DWARF tag for the node.
  /// \param LineNo Source line number.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param DataSizeInBits Number of value bits used by the object.
  /// \param Flags Flags bitfield.
  /// \param Ops Operand list for the type.
  DIBasicType(LLVMContext &C, StorageType Storage, unsigned Tag,
              unsigned LineNo, uint32_t AlignInBits, unsigned Encoding,
              uint32_t NumExtraInhabitants, uint32_t DataSizeInBits,
              DIFlags Flags, ArrayRef<Metadata *> Ops)
      : DIType(C, DIBasicTypeKind, Storage, Tag, LineNo, AlignInBits,
               NumExtraInhabitants, Flags, Ops),
        Encoding(Encoding), DataSizeInBits(DataSizeInBits) {}
  /// Construct a DIBasicType with an explicit metadata subclass ID.
  /// \param C LLVM context that owns the metadata.
  /// \param ID Metadata subclass ID.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Tag DWARF tag for the node.
  /// \param LineNo Source line number.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param DataSizeInBits Number of value bits used by the object.
  /// \param Flags Flags bitfield.
  /// \param Ops Operand list for the type.
  DIBasicType(LLVMContext &C, unsigned ID, StorageType Storage, unsigned Tag,
              unsigned LineNo, uint32_t AlignInBits, unsigned Encoding,
              uint32_t NumExtraInhabitants, uint32_t DataSizeInBits,
              DIFlags Flags, ArrayRef<Metadata *> Ops)
      : DIType(C, ID, Storage, Tag, LineNo, AlignInBits, NumExtraInhabitants,
               Flags, Ops),
        Encoding(Encoding), DataSizeInBits(DataSizeInBits) {}
  /// Destroy this DIBasicType.
  ~DIBasicType() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param DataSizeInBits Number of value bits used by the object.
  /// \param Flags Flags bitfield.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIBasicType *getImpl(LLVMContext &Context, unsigned Tag,
                              StringRef Name, DIFile *File, unsigned LineNo,
                              DIScope *Scope, uint64_t SizeInBits,
                              uint32_t AlignInBits, unsigned Encoding,
                              uint32_t NumExtraInhabitants,
                              uint32_t DataSizeInBits, DIFlags Flags,
                              StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Tag, getCanonicalMDString(Context, Name), File,
                   LineNo, Scope, SizeInBits, AlignInBits, Encoding,
                   NumExtraInhabitants, DataSizeInBits, Flags, Storage,
                   ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param DataSizeInBits Number of value bits used by the object.
  /// \param Flags Flags bitfield.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIBasicType *getImpl(LLVMContext &Context, unsigned Tag,
                              MDString *Name, DIFile *File, unsigned LineNo,
                              DIScope *Scope, uint64_t SizeInBits,
                              uint32_t AlignInBits, unsigned Encoding,
                              uint32_t NumExtraInhabitants,
                              uint32_t DataSizeInBits, DIFlags Flags,
                              StorageType Storage, bool ShouldCreate = true) {
    auto *SizeInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), SizeInBits));
    return getImpl(Context, Tag, Name, File, LineNo, Scope, SizeInBitsNode,
                   AlignInBits, Encoding, NumExtraInhabitants, DataSizeInBits,
                   Flags, Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param DataSizeInBits Number of value bits used by the object.
  /// \param Flags Flags bitfield.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIBasicType *
  getImpl(LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *File,
          unsigned LineNo, Metadata *Scope, Metadata *SizeInBits,
          uint32_t AlignInBits, unsigned Encoding, uint32_t NumExtraInhabitants,
          uint32_t DataSizeInBits, DIFlags Flags, StorageType Storage,
          bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIBasicType cloneImpl() const {
    return getTemporary(
        getContext(), getTag(), getRawName(), getFile(), getLine(), getScope(),
        getRawSizeInBits(), getAlignInBits(), getEncoding(),
        getNumExtraInhabitants(), getDataSizeInBits(), getFlags());
  }

public:
  /// Get or create a DIBasicType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIBasicType, (unsigned Tag, StringRef Name),
                    (Tag, Name, nullptr, 0, nullptr, 0, 0, 0, 0, 0, FlagZero))
  /// Get or create a DIBasicType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param SizeInBits Size of the type in bits.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIBasicType,
                    (unsigned Tag, StringRef Name, uint64_t SizeInBits),
                    (Tag, Name, nullptr, 0, nullptr, SizeInBits, 0, 0, 0, 0,
                     FlagZero))
  /// Get or create a DIBasicType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param SizeInBits Size of the type in bits.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIBasicType,
                    (unsigned Tag, MDString *Name, uint64_t SizeInBits),
                    (Tag, Name, nullptr, 0, nullptr, SizeInBits, 0, 0, 0, 0,
                     FlagZero))
  /// Get or create a DIBasicType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param Flags Flags bitfield.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIBasicType,
                    (unsigned Tag, StringRef Name, uint64_t SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding, DIFlags Flags),
                    (Tag, Name, nullptr, 0, nullptr, SizeInBits, AlignInBits,
                     Encoding, 0, 0, Flags))
  /// Get or create a DIBasicType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param Flags Flags bitfield.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIBasicType,
                    (unsigned Tag, MDString *Name, uint64_t SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding, DIFlags Flags),
                    (Tag, Name, nullptr, 0, nullptr, SizeInBits, AlignInBits,
                     Encoding, 0, 0, Flags))
  /// Get or create a DIBasicType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param Flags Flags bitfield.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIBasicType,
                    (unsigned Tag, StringRef Name, uint64_t SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding,
                     uint32_t NumExtraInhabitants, DIFlags Flags),
                    (Tag, Name, nullptr, 0, nullptr, SizeInBits, AlignInBits,
                     Encoding, NumExtraInhabitants, 0, Flags))
  /// Get or create a DIBasicType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param DataSizeInBits Number of value bits used by the object.
  /// \param Flags Flags bitfield.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIBasicType,
                    (unsigned Tag, StringRef Name, uint64_t SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding,
                     uint32_t NumExtraInhabitants, uint32_t DataSizeInBits,
                     DIFlags Flags),
                    (Tag, Name, nullptr, 0, nullptr, SizeInBits, AlignInBits,
                     Encoding, NumExtraInhabitants, DataSizeInBits, Flags))
  /// Get or create a DIBasicType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param DataSizeInBits Number of value bits used by the object.
  /// \param Flags Flags bitfield.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIBasicType,
                    (unsigned Tag, StringRef Name, DIFile *File,
                     unsigned LineNo, DIScope *Scope, uint64_t SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding,
                     uint32_t NumExtraInhabitants, uint32_t DataSizeInBits,
                     DIFlags Flags),
                    (Tag, Name, File, LineNo, Scope, SizeInBits, AlignInBits,
                     Encoding, NumExtraInhabitants, DataSizeInBits, Flags))
  /// Get or create a DIBasicType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param DataSizeInBits Number of value bits used by the object.
  /// \param Flags Flags bitfield.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIBasicType,
                    (unsigned Tag, MDString *Name, uint64_t SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding,
                     uint32_t NumExtraInhabitants, uint32_t DataSizeInBits,
                     DIFlags Flags),
                    (Tag, Name, nullptr, 0, nullptr, SizeInBits, AlignInBits,
                     Encoding, NumExtraInhabitants, DataSizeInBits, Flags))
  /// Get or create a DIBasicType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param DataSizeInBits Number of value bits used by the object.
  /// \param Flags Flags bitfield.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIBasicType,
                    (unsigned Tag, MDString *Name, Metadata *SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding,
                     uint32_t NumExtraInhabitants, uint32_t DataSizeInBits,
                     DIFlags Flags),
                    (Tag, Name, nullptr, 0, nullptr, SizeInBits, AlignInBits,
                     Encoding, NumExtraInhabitants, DataSizeInBits, Flags))
  /// Get or create a DIBasicType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param DataSizeInBits Number of value bits used by the object.
  /// \param Flags Flags bitfield.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIBasicType,
                    (unsigned Tag, MDString *Name, Metadata *File,
                     unsigned LineNo, Metadata *Scope, Metadata *SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding,
                     uint32_t NumExtraInhabitants, uint32_t DataSizeInBits,
                     DIFlags Flags),
                    (Tag, Name, File, LineNo, Scope, SizeInBits, AlignInBits,
                     Encoding, NumExtraInhabitants, DataSizeInBits, Flags))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIBasicType clone() const { return cloneImpl(); }

  /// Return the encoding.
  /// \return The encoding.
  unsigned getEncoding() const { return Encoding; }

  /// Return the data size in bits.
  /// \return The data size in bits.
  uint32_t getDataSizeInBits() const { return DataSizeInBits; }

  /// Whether a basic type is signed or unsigned.
  enum class Signedness {
    /// Signed integer or character type.
    Signed,
    /// Unsigned integer or character type.
    Unsigned
  };

  /// Return the signedness of this type, or std::nullopt if this type is
  /// neither signed nor unsigned.
  /// \return The signedness of this type, or std::nullopt if this type is.
  LLVM_ABI std::optional<Signedness> getSignedness() const;

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIBasicTypeKind ||
           MD->getMetadataID() == DIFixedPointTypeKind;
  }
};

/// Fixed-point type.
class DIFixedPointType : public DIBasicType {
  friend class LLVMContextImpl;
  friend class MDNode;

  // Actually FixedPointKind.
  unsigned Kind;
  // Used for binary and decimal.
  int Factor;
  // Used for rational.
  APInt Numerator;
  APInt Denominator;

  DIFixedPointType(LLVMContext &C, StorageType Storage, unsigned Tag,
                   unsigned LineNo, uint32_t AlignInBits, unsigned Encoding,
                   DIFlags Flags, unsigned Kind, int Factor,
                   ArrayRef<Metadata *> Ops)
      : DIBasicType(C, DIFixedPointTypeKind, Storage, Tag, LineNo, AlignInBits,
                    Encoding, 0, 0, Flags, Ops),
        Kind(Kind), Factor(Factor) {
    assert(Kind == FixedPointBinary || Kind == FixedPointDecimal);
  }
  DIFixedPointType(LLVMContext &C, StorageType Storage, unsigned Tag,
                   unsigned LineNo, uint32_t AlignInBits, unsigned Encoding,
                   DIFlags Flags, unsigned Kind, APInt Numerator,
                   APInt Denominator, ArrayRef<Metadata *> Ops)
      : DIBasicType(C, DIFixedPointTypeKind, Storage, Tag, LineNo, AlignInBits,
                    Encoding, 0, 0, Flags, Ops),
        Kind(Kind), Factor(0), Numerator(Numerator), Denominator(Denominator) {
    assert(Kind == FixedPointRational);
  }
  DIFixedPointType(LLVMContext &C, StorageType Storage, unsigned Tag,
                   unsigned LineNo, uint32_t AlignInBits, unsigned Encoding,
                   DIFlags Flags, unsigned Kind, int Factor, APInt Numerator,
                   APInt Denominator, ArrayRef<Metadata *> Ops)
      : DIBasicType(C, DIFixedPointTypeKind, Storage, Tag, LineNo, AlignInBits,
                    Encoding, 0, 0, Flags, Ops),
        Kind(Kind), Factor(Factor), Numerator(Numerator),
        Denominator(Denominator) {}
  /// Destroy this DIFixedPointType.
  ~DIFixedPointType() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param Flags Flags bitfield.
  /// \param Kind Kind enumerator value.
  /// \param Factor Fixed-point scale factor.
  /// \param Numerator Numerator of a rational fixed-point scale.
  /// \param Denominator Denominator of a rational fixed-point scale.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIFixedPointType *
  getImpl(LLVMContext &Context, unsigned Tag, StringRef Name, DIFile *File,
          unsigned LineNo, DIScope *Scope, uint64_t SizeInBits,
          uint32_t AlignInBits, unsigned Encoding, DIFlags Flags, unsigned Kind,
          int Factor, APInt Numerator, APInt Denominator, StorageType Storage,
          bool ShouldCreate = true) {
    auto *SizeInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), SizeInBits));
    return getImpl(Context, Tag, getCanonicalMDString(Context, Name), File,
                   LineNo, Scope, SizeInBitsNode, AlignInBits, Encoding, Flags,
                   Kind, Factor, Numerator, Denominator, Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param Flags Flags bitfield.
  /// \param Kind Kind enumerator value.
  /// \param Factor Fixed-point scale factor.
  /// \param Numerator Numerator of a rational fixed-point scale.
  /// \param Denominator Denominator of a rational fixed-point scale.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIFixedPointType *
  getImpl(LLVMContext &Context, unsigned Tag, StringRef Name, DIFile *File,
          unsigned LineNo, DIScope *Scope, Metadata *SizeInBits,
          uint32_t AlignInBits, unsigned Encoding, DIFlags Flags, unsigned Kind,
          int Factor, APInt Numerator, APInt Denominator, StorageType Storage,
          bool ShouldCreate = true) {
    return getImpl(Context, Tag, getCanonicalMDString(Context, Name), File,
                   LineNo, Scope, SizeInBits, AlignInBits, Encoding, Flags,
                   Kind, Factor, Numerator, Denominator, Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param Flags Flags bitfield.
  /// \param Kind Kind enumerator value.
  /// \param Factor Fixed-point scale factor.
  /// \param Numerator Numerator of a rational fixed-point scale.
  /// \param Denominator Denominator of a rational fixed-point scale.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIFixedPointType *
  getImpl(LLVMContext &Context, unsigned Tag, MDString *Name, DIFile *File,
          unsigned LineNo, DIScope *Scope, uint64_t SizeInBits,
          uint32_t AlignInBits, unsigned Encoding, DIFlags Flags, unsigned Kind,
          int Factor, APInt Numerator, APInt Denominator, StorageType Storage,
          bool ShouldCreate = true) {
    auto *SizeInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), SizeInBits));
    return getImpl(Context, Tag, Name, File, LineNo, Scope, SizeInBitsNode,
                   AlignInBits, Encoding, Flags, Kind, Factor, Numerator,
                   Denominator, Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param Flags Flags bitfield.
  /// \param Kind Kind enumerator value.
  /// \param Factor Fixed-point scale factor.
  /// \param Numerator Numerator of a rational fixed-point scale.
  /// \param Denominator Denominator of a rational fixed-point scale.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIFixedPointType *
  getImpl(LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *File,
          unsigned LineNo, Metadata *Scope, Metadata *SizeInBits,
          uint32_t AlignInBits, unsigned Encoding, DIFlags Flags, unsigned Kind,
          int Factor, APInt Numerator, APInt Denominator, StorageType Storage,
          bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIFixedPointType cloneImpl() const {
    return getTemporary(getContext(), getTag(), getRawName(), getFile(),
                        getLine(), getScope(), getRawSizeInBits(),
                        getAlignInBits(), getEncoding(), getFlags(), Kind,
                        Factor, Numerator, Denominator);
  }

public:
  /// Kind of fixed-point scale factor encoding.
  enum FixedPointKind : unsigned {
    /// Scale factor is a power of two (2^Factor).
    /// \return scale factor is a power of two (2^Factor).
    FixedPointBinary,
    /// Scale factor is a power of ten (10^Factor).
    FixedPointDecimal,
    /// Scale factor is an arbitrary rational Numerator/Denominator.
    FixedPointRational,
    /// Last valid fixed-point kind.
    LastFixedPointKind = FixedPointRational,
  };

  /// Return the fixed point kind.
  /// \param Str String to parse.
  /// \return The fixed point kind.
  LLVM_ABI static std::optional<FixedPointKind>
  getFixedPointKind(StringRef Str);
  /// Return the string name for a fixed-point kind.
  /// \param Kind Fixed-point kind to name.
  /// \return The string name for a fixed-point kind.
  LLVM_ABI static const char *fixedPointKindString(FixedPointKind Kind);

  /// Get or create a DIFixedPointType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param Flags Flags bitfield.
  /// \param Kind Kind enumerator value.
  /// \param Factor Fixed-point scale factor.
  /// \param Numerator Numerator of a rational fixed-point scale.
  /// \param Denominator Denominator of a rational fixed-point scale.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIFixedPointType,
                    (unsigned Tag, MDString *Name, DIFile *File,
                     unsigned LineNo, DIScope *Scope, uint64_t SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding, DIFlags Flags,
                     unsigned Kind, int Factor, APInt Numerator,
                     APInt Denominator),
                    (Tag, Name, File, LineNo, Scope, SizeInBits, AlignInBits,
                     Encoding, Flags, Kind, Factor, Numerator, Denominator))
  /// Get or create a DIFixedPointType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param Flags Flags bitfield.
  /// \param Kind Kind enumerator value.
  /// \param Factor Fixed-point scale factor.
  /// \param Numerator Numerator of a rational fixed-point scale.
  /// \param Denominator Denominator of a rational fixed-point scale.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIFixedPointType,
                    (unsigned Tag, StringRef Name, DIFile *File,
                     unsigned LineNo, DIScope *Scope, uint64_t SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding, DIFlags Flags,
                     unsigned Kind, int Factor, APInt Numerator,
                     APInt Denominator),
                    (Tag, Name, File, LineNo, Scope, SizeInBits, AlignInBits,
                     Encoding, Flags, Kind, Factor, Numerator, Denominator))
  /// Get or create a DIFixedPointType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param Flags Flags bitfield.
  /// \param Kind Kind enumerator value.
  /// \param Factor Fixed-point scale factor.
  /// \param Numerator Numerator of a rational fixed-point scale.
  /// \param Denominator Denominator of a rational fixed-point scale.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIFixedPointType,
                    (unsigned Tag, MDString *Name, Metadata *File,
                     unsigned LineNo, Metadata *Scope, Metadata *SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding, DIFlags Flags,
                     unsigned Kind, int Factor, APInt Numerator,
                     APInt Denominator),
                    (Tag, Name, File, LineNo, Scope, SizeInBits, AlignInBits,
                     Encoding, Flags, Kind, Factor, Numerator, Denominator))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIFixedPointType clone() const { return cloneImpl(); }

  /// Return true if this is binary.
  /// \return true if this is binary.
  bool isBinary() const { return Kind == FixedPointBinary; }
  /// Return true if this is decimal.
  /// \return true if this is decimal.
  bool isDecimal() const { return Kind == FixedPointDecimal; }
  /// Return true if this is rational.
  /// \return true if this is rational.
  bool isRational() const { return Kind == FixedPointRational; }

  /// Return true if this is signed.
  /// \return true if this is signed.
  LLVM_ABI bool isSigned() const;

  /// Return the kind.
  /// \return The kind.
  FixedPointKind getKind() const { return static_cast<FixedPointKind>(Kind); }

  /// Return the factor raw.
  /// \return The factor raw.
  int getFactorRaw() const { return Factor; }
  /// Return the factor.
  /// \return The factor.
  int getFactor() const {
    assert(Kind == FixedPointBinary || Kind == FixedPointDecimal);
    return Factor;
  }

  /// Return the numerator raw.
  /// \return The numerator raw.
  const APInt &getNumeratorRaw() const { return Numerator; }
  /// Return the numerator.
  /// \return The numerator.
  const APInt &getNumerator() const {
    assert(Kind == FixedPointRational);
    return Numerator;
  }

  /// Return the denominator raw.
  /// \return The denominator raw.
  const APInt &getDenominatorRaw() const { return Denominator; }
  /// Return the denominator.
  /// \return The denominator.
  const APInt &getDenominator() const {
    assert(Kind == FixedPointRational);
    return Denominator;
  }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIFixedPointTypeKind;
  }
};

/// String type, Fortran CHARACTER(n)
class DIStringType : public DIType {
  friend class LLVMContextImpl;
  friend class MDNode;

  static constexpr unsigned MY_FIRST_OPERAND = DIType::N_OPERANDS;

  unsigned Encoding;

  DIStringType(LLVMContext &C, StorageType Storage, unsigned Tag,
               uint32_t AlignInBits, unsigned Encoding,
               ArrayRef<Metadata *> Ops)
      : DIType(C, DIStringTypeKind, Storage, Tag, 0, AlignInBits, 0, FlagZero,
               Ops),
        Encoding(Encoding) {}
  /// Destroy this DIStringType.
  ~DIStringType() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param StringLength Metadata describing string length.
  /// \param StrLenExp The str len exp.
  /// \param StrLocationExp The str location exp.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIStringType *getImpl(LLVMContext &Context, unsigned Tag,
                               StringRef Name, Metadata *StringLength,
                               Metadata *StrLenExp, Metadata *StrLocationExp,
                               uint64_t SizeInBits, uint32_t AlignInBits,
                               unsigned Encoding, StorageType Storage,
                               bool ShouldCreate = true) {
    auto *SizeInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), SizeInBits));
    return getImpl(Context, Tag, getCanonicalMDString(Context, Name),
                   StringLength, StrLenExp, StrLocationExp, SizeInBitsNode,
                   AlignInBits, Encoding, Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param StringLength Metadata describing string length.
  /// \param StrLenExp The str len exp.
  /// \param StrLocationExp The str location exp.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIStringType *getImpl(LLVMContext &Context, unsigned Tag,
                               MDString *Name, Metadata *StringLength,
                               Metadata *StrLenExp, Metadata *StrLocationExp,
                               uint64_t SizeInBits, uint32_t AlignInBits,
                               unsigned Encoding, StorageType Storage,
                               bool ShouldCreate = true) {
    auto *SizeInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), SizeInBits));
    return getImpl(Context, Tag, Name, StringLength, StrLenExp, StrLocationExp,
                   SizeInBitsNode, AlignInBits, Encoding, Storage,
                   ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param StringLength Metadata describing string length.
  /// \param StrLenExp The str len exp.
  /// \param StrLocationExp The str location exp.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIStringType *
  getImpl(LLVMContext &Context, unsigned Tag, MDString *Name,
          Metadata *StringLength, Metadata *StrLenExp, Metadata *StrLocationExp,
          Metadata *SizeInBits, uint32_t AlignInBits, unsigned Encoding,
          StorageType Storage, bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIStringType cloneImpl() const {
    return getTemporary(getContext(), getTag(), getRawName(),
                        getRawStringLength(), getRawStringLengthExp(),
                        getRawStringLocationExp(), getRawSizeInBits(),
                        getAlignInBits(), getEncoding());
  }

public:
  /// Get or create a DIStringType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIStringType,
                    (unsigned Tag, StringRef Name, uint64_t SizeInBits,
                     uint32_t AlignInBits),
                    (Tag, Name, nullptr, nullptr, nullptr, SizeInBits,
                     AlignInBits, 0))
  /// Get or create a DIStringType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param StringLength Metadata describing string length.
  /// \param StringLengthExp DIExpression for the string length.
  /// \param StringLocationExp DIExpression for the string location.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIStringType,
                    (unsigned Tag, MDString *Name, Metadata *StringLength,
                     Metadata *StringLengthExp, Metadata *StringLocationExp,
                     uint64_t SizeInBits, uint32_t AlignInBits,
                     unsigned Encoding),
                    (Tag, Name, StringLength, StringLengthExp,
                     StringLocationExp, SizeInBits, AlignInBits, Encoding))
  /// Get or create a DIStringType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param StringLength Metadata describing string length.
  /// \param StringLengthExp DIExpression for the string length.
  /// \param StringLocationExp DIExpression for the string location.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIStringType,
                    (unsigned Tag, StringRef Name, Metadata *StringLength,
                     Metadata *StringLengthExp, Metadata *StringLocationExp,
                     uint64_t SizeInBits, uint32_t AlignInBits,
                     unsigned Encoding),
                    (Tag, Name, StringLength, StringLengthExp,
                     StringLocationExp, SizeInBits, AlignInBits, Encoding))
  /// Get or create a DIStringType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param StringLength Metadata describing string length.
  /// \param StringLengthExp DIExpression for the string length.
  /// \param StringLocationExp DIExpression for the string location.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Encoding DWARF encoding attribute.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIStringType,
                    (unsigned Tag, MDString *Name, Metadata *StringLength,
                     Metadata *StringLengthExp, Metadata *StringLocationExp,
                     Metadata *SizeInBits, uint32_t AlignInBits,
                     unsigned Encoding),
                    (Tag, Name, StringLength, StringLengthExp,
                     StringLocationExp, SizeInBits, AlignInBits, Encoding))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIStringType clone() const { return cloneImpl(); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIStringTypeKind;
  }

  /// Return the string length.
  /// \return The string length.
  DIVariable *getStringLength() const {
    return cast_or_null<DIVariable>(getRawStringLength());
  }

  /// Return the string length exp.
  /// \return The string length exp.
  DIExpression *getStringLengthExp() const {
    return cast_or_null<DIExpression>(getRawStringLengthExp());
  }

  /// Return the string location exp.
  /// \return The string location exp.
  DIExpression *getStringLocationExp() const {
    return cast_or_null<DIExpression>(getRawStringLocationExp());
  }

  /// Return the encoding.
  /// \return The encoding.
  unsigned getEncoding() const { return Encoding; }

  /// Return the raw string length operand.
  /// \return The raw string length operand.
  Metadata *getRawStringLength() const { return getOperand(MY_FIRST_OPERAND); }

  /// Return the raw string length exp operand.
  /// \return The raw string length exp operand.
  Metadata *getRawStringLengthExp() const {
    return getOperand(MY_FIRST_OPERAND + 1);
  }

  /// Return the raw string location exp operand.
  /// \return The raw string location exp operand.
  Metadata *getRawStringLocationExp() const {
    return getOperand(MY_FIRST_OPERAND + 2);
  }
};

/// Derived types.
///
/// This includes qualified types, pointers, references, friends, typedefs, and
/// class members.
///
/// TODO: Split out members (inheritance, fields, methods, etc.).
class DIDerivedType : public DIType {
public:
  /// Pointer authentication (__ptrauth) metadata.
  struct PtrAuthData {
    // RawData layout:
    // - Bits 0..3:  Key
    // - Bit  4:     IsAddressDiscriminated
    // - Bits 5..20: ExtraDiscriminator
    // - Bit  21:    IsaPointer
    // - Bit  22:    AuthenticatesNullValues
    /// Packed pointer-authentication data word.
    unsigned RawData;

    /// Packed pointer-authentication data word.
    /// \param FromRawData The from raw data.
    PtrAuthData(unsigned FromRawData) : RawData(FromRawData) {}
    /// Construct a PtrAuthData.
    /// \param Key The key.
    /// \param IsDiscr The is discr.
    /// \param Discriminator Encoded discriminator value.
    /// \param IsaPointer The isa pointer.
    /// \param AuthenticatesNullValues The authenticates null values.
    PtrAuthData(unsigned Key, bool IsDiscr, unsigned Discriminator,
                bool IsaPointer, bool AuthenticatesNullValues) {
      assert(Key < 16);
      assert(Discriminator <= 0xffff);
      RawData = (Key << 0) | (IsDiscr ? (1 << 4) : 0) | (Discriminator << 5) |
                (IsaPointer ? (1 << 21) : 0) |
                (AuthenticatesNullValues ? (1 << 22) : 0);
    }

    /// Return the pointer-authentication key.
    /// \return The pointer-authentication key.
    unsigned key() { return (RawData >> 0) & 0b1111; }
    /// Return true if this is address discriminated.
    /// \return true if this is address discriminated.
    bool isAddressDiscriminated() { return (RawData >> 4) & 1; }
    /// Return the extra discriminator value.
    /// \return The extra discriminator value.
    unsigned extraDiscriminator() { return (RawData >> 5) & 0xffff; }
    /// Return true if the signed value is a pointer.
    /// \return true if the signed value is a pointer.
    bool isaPointer() { return (RawData >> 21) & 1; }
    /// Return true if null values are authenticated.
    /// \return true if null values are authenticated.
    bool authenticatesNullValues() { return (RawData >> 22) & 1; }
  };

private:
  friend class LLVMContextImpl;
  friend class MDNode;

  static constexpr unsigned MY_FIRST_OPERAND = DIType::N_OPERANDS;

  /// The DWARF address space of the memory pointed to or referenced by a
  /// pointer or reference type respectively.
  std::optional<unsigned> DWARFAddressSpace;

  DIDerivedType(LLVMContext &C, StorageType Storage, unsigned Tag,
                unsigned Line, uint32_t AlignInBits,
                std::optional<unsigned> DWARFAddressSpace,
                std::optional<PtrAuthData> PtrAuthData, DIFlags Flags,
                ArrayRef<Metadata *> Ops)
      : DIType(C, DIDerivedTypeKind, Storage, Tag, Line, AlignInBits, 0, Flags,
               Ops),
        DWARFAddressSpace(DWARFAddressSpace) {
    if (PtrAuthData)
      SubclassData32 = PtrAuthData->RawData;
  }
  /// Destroy this DIDerivedType.
  ~DIDerivedType() = default;
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param DWARFAddressSpace Optional DWARF address space.
  /// \param PtrAuthData Optional pointer-authentication data.
  /// \param Flags Flags bitfield.
  /// \param ExtraData Extra derived-type operand.
  /// \param Annotations Annotation metadata tuple.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIDerivedType *
  getImpl(LLVMContext &Context, unsigned Tag, StringRef Name, DIFile *File,
          unsigned Line, DIScope *Scope, DIType *BaseType, uint64_t SizeInBits,
          uint32_t AlignInBits, uint64_t OffsetInBits,
          std::optional<unsigned> DWARFAddressSpace,
          std::optional<PtrAuthData> PtrAuthData, DIFlags Flags,
          Metadata *ExtraData, DINodeArray Annotations, StorageType Storage,
          bool ShouldCreate = true) {
    auto *SizeInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), SizeInBits));
    auto *OffsetInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), OffsetInBits));
    return getImpl(Context, Tag, getCanonicalMDString(Context, Name), File,
                   Line, Scope, BaseType, SizeInBitsNode, AlignInBits,
                   OffsetInBitsNode, DWARFAddressSpace, PtrAuthData, Flags,
                   ExtraData, Annotations.get(), Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param DWARFAddressSpace Optional DWARF address space.
  /// \param PtrAuthData Optional pointer-authentication data.
  /// \param Flags Flags bitfield.
  /// \param ExtraData Extra derived-type operand.
  /// \param Annotations Annotation metadata tuple.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIDerivedType *
  getImpl(LLVMContext &Context, unsigned Tag, MDString *Name, DIFile *File,
          unsigned Line, DIScope *Scope, DIType *BaseType, uint64_t SizeInBits,
          uint32_t AlignInBits, uint64_t OffsetInBits,
          std::optional<unsigned> DWARFAddressSpace,
          std::optional<PtrAuthData> PtrAuthData, DIFlags Flags,
          Metadata *ExtraData, DINodeArray Annotations, StorageType Storage,
          bool ShouldCreate = true) {
    auto *SizeInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), SizeInBits));
    auto *OffsetInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), OffsetInBits));
    return getImpl(Context, Tag, Name, File, Line, Scope, BaseType,
                   SizeInBitsNode, AlignInBits, OffsetInBitsNode,
                   DWARFAddressSpace, PtrAuthData, Flags, ExtraData,
                   Annotations.get(), Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param DWARFAddressSpace Optional DWARF address space.
  /// \param PtrAuthData Optional pointer-authentication data.
  /// \param Flags Flags bitfield.
  /// \param ExtraData Extra derived-type operand.
  /// \param Annotations Annotation metadata tuple.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIDerivedType *
  getImpl(LLVMContext &Context, unsigned Tag, StringRef Name, DIFile *File,
          unsigned Line, DIScope *Scope, DIType *BaseType, Metadata *SizeInBits,
          uint32_t AlignInBits, Metadata *OffsetInBits,
          std::optional<unsigned> DWARFAddressSpace,
          std::optional<PtrAuthData> PtrAuthData, DIFlags Flags,
          Metadata *ExtraData, DINodeArray Annotations, StorageType Storage,
          bool ShouldCreate = true) {
    return getImpl(Context, Tag, getCanonicalMDString(Context, Name), File,
                   Line, Scope, BaseType, SizeInBits, AlignInBits, OffsetInBits,
                   DWARFAddressSpace, PtrAuthData, Flags, ExtraData,
                   Annotations.get(), Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param DWARFAddressSpace Optional DWARF address space.
  /// \param PtrAuthData Optional pointer-authentication data.
  /// \param Flags Flags bitfield.
  /// \param ExtraData Extra derived-type operand.
  /// \param Annotations Annotation metadata tuple.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIDerivedType *
  getImpl(LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *File,
          unsigned Line, Metadata *Scope, Metadata *BaseType,
          Metadata *SizeInBits, uint32_t AlignInBits, Metadata *OffsetInBits,
          std::optional<unsigned> DWARFAddressSpace,
          std::optional<PtrAuthData> PtrAuthData, DIFlags Flags,
          Metadata *ExtraData, Metadata *Annotations, StorageType Storage,
          bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIDerivedType cloneImpl() const {
    return getTemporary(
        getContext(), getTag(), getRawName(), getFile(), getLine(), getScope(),
        getBaseType(), getRawSizeInBits(), getAlignInBits(),
        getRawOffsetInBits(), getDWARFAddressSpace(), getPtrAuthData(),
        getFlags(), getExtraData(), getRawAnnotations());
  }

public:
  /// Get or create a DIDerivedType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param DWARFAddressSpace Optional DWARF address space.
  /// \param PtrAuthData Optional pointer-authentication data.
  /// \param Flags Flags bitfield.
  /// \param ExtraData Extra derived-type operand.
  /// \param Annotations Annotation metadata tuple.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIDerivedType,
                    (unsigned Tag, MDString *Name, Metadata *File,
                     unsigned Line, Metadata *Scope, Metadata *BaseType,
                     Metadata *SizeInBits, uint32_t AlignInBits,
                     Metadata *OffsetInBits,
                     std::optional<unsigned> DWARFAddressSpace,
                     std::optional<PtrAuthData> PtrAuthData, DIFlags Flags,
                     Metadata *ExtraData = nullptr,
                     Metadata *Annotations = nullptr),
                    (Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                     AlignInBits, OffsetInBits, DWARFAddressSpace, PtrAuthData,
                     Flags, ExtraData, Annotations))
  /// Get or create a DIDerivedType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param DWARFAddressSpace Optional DWARF address space.
  /// \param PtrAuthData Optional pointer-authentication data.
  /// \param Flags Flags bitfield.
  /// \param ExtraData Extra derived-type operand.
  /// \param Annotations Annotation metadata tuple.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIDerivedType,
                    (unsigned Tag, StringRef Name, DIFile *File, unsigned Line,
                     DIScope *Scope, DIType *BaseType, Metadata *SizeInBits,
                     uint32_t AlignInBits, Metadata *OffsetInBits,
                     std::optional<unsigned> DWARFAddressSpace,
                     std::optional<PtrAuthData> PtrAuthData, DIFlags Flags,
                     Metadata *ExtraData = nullptr,
                     DINodeArray Annotations = nullptr),
                    (Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                     AlignInBits, OffsetInBits, DWARFAddressSpace, PtrAuthData,
                     Flags, ExtraData, Annotations))
  /// Get or create a DIDerivedType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param DWARFAddressSpace Optional DWARF address space.
  /// \param PtrAuthData Optional pointer-authentication data.
  /// \param Flags Flags bitfield.
  /// \param ExtraData Extra derived-type operand.
  /// \param Annotations Annotation metadata tuple.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIDerivedType,
                    (unsigned Tag, MDString *Name, DIFile *File, unsigned Line,
                     DIScope *Scope, DIType *BaseType, uint64_t SizeInBits,
                     uint32_t AlignInBits, uint64_t OffsetInBits,
                     std::optional<unsigned> DWARFAddressSpace,
                     std::optional<PtrAuthData> PtrAuthData, DIFlags Flags,
                     Metadata *ExtraData = nullptr,
                     DINodeArray Annotations = nullptr),
                    (Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                     AlignInBits, OffsetInBits, DWARFAddressSpace, PtrAuthData,
                     Flags, ExtraData, Annotations))
  /// Get or create a DIDerivedType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param DWARFAddressSpace Optional DWARF address space.
  /// \param PtrAuthData Optional pointer-authentication data.
  /// \param Flags Flags bitfield.
  /// \param ExtraData Extra derived-type operand.
  /// \param Annotations Annotation metadata tuple.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIDerivedType,
                    (unsigned Tag, StringRef Name, DIFile *File, unsigned Line,
                     DIScope *Scope, DIType *BaseType, uint64_t SizeInBits,
                     uint32_t AlignInBits, uint64_t OffsetInBits,
                     std::optional<unsigned> DWARFAddressSpace,
                     std::optional<PtrAuthData> PtrAuthData, DIFlags Flags,
                     Metadata *ExtraData = nullptr,
                     DINodeArray Annotations = nullptr),
                    (Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                     AlignInBits, OffsetInBits, DWARFAddressSpace, PtrAuthData,
                     Flags, ExtraData, Annotations))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIDerivedType clone() const { return cloneImpl(); }

  /// Get the base type this is derived from.
  /// \return get the base type this is derived from.
  DIType *getBaseType() const { return cast_or_null<DIType>(getRawBaseType()); }
  /// Return the raw base type operand.
  /// \return The raw base type operand.
  Metadata *getRawBaseType() const { return getOperand(MY_FIRST_OPERAND); }

  /// Return the DWARF address space of the pointed-to or referenced memory.
  ///
  /// \returns The DWARF address space of the memory pointed to or referenced by
  /// a pointer or reference type respectively.
  std::optional<unsigned> getDWARFAddressSpace() const {
    return DWARFAddressSpace;
  }

  /// Return the ptr auth data.
  /// \return The ptr auth data.
  LLVM_ABI std::optional<PtrAuthData> getPtrAuthData() const;

  /// Get extra data associated with this derived type.
  ///
  /// Class type for pointer-to-members, objective-c property node for ivars,
  /// global constant wrapper for static members, virtual base pointer offset
  /// for inheritance, a tuple of template parameters for template aliases,
  /// discriminant for a variant, or storage offset for a bit field.
  ///
  /// TODO: Separate out types that need this extra operand: pointer-to-member
  /// types and member fields (static members and ivars).
  /// \return get extra data associated with this derived type.
  Metadata *getExtraData() const { return getRawExtraData(); }
  /// Return the raw extra data operand.
  /// \return The raw extra data operand.
  Metadata *getRawExtraData() const { return getOperand(MY_FIRST_OPERAND + 1); }

  /// Get the template parameters from a template alias.
  /// \return get the template parameters from a template alias.
  DITemplateParameterArray getTemplateParams() const {
    return cast_or_null<MDTuple>(getExtraData());
  }

  /// Get annotations associated with this derived type.
  /// \return get annotations associated with this derived type.
  DINodeArray getAnnotations() const {
    return cast_or_null<MDTuple>(getRawAnnotations());
  }
  /// Return the raw annotations operand.
  /// \return The raw annotations operand.
  Metadata *getRawAnnotations() const {
    return getOperand(MY_FIRST_OPERAND + 2);
  }

  /// Get casted version of extra data.
  /// @{
  /// \return get casted version of extra data.
  LLVM_ABI DIType *getClassType() const;

  /// Return the obj c property.
  /// \return The obj c property.
  DIObjCProperty *getObjCProperty() const {
    return dyn_cast_or_null<DIObjCProperty>(getExtraData());
  }

  /// Return the vb ptr offset.
  /// \return The vb ptr offset.
  LLVM_ABI uint32_t getVBPtrOffset() const;

  /// Return the storage offset in bits.
  /// \return The storage offset in bits.
  LLVM_ABI Constant *getStorageOffsetInBits() const;

  /// Return the constant.
  /// \return The constant.
  LLVM_ABI Constant *getConstant() const;

  /// Return the discriminant value.
  /// \return The discriminant value.
  LLVM_ABI Constant *getDiscriminantValue() const;
  /// @}

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIDerivedTypeKind;
  }
};

/// Return true if the two values compare equal.
/// \param Lhs Left-hand pointer-auth data.
/// \param Rhs Right-hand pointer-auth data.
/// \return true if the two values compare equal.
inline bool operator==(DIDerivedType::PtrAuthData Lhs,
                       DIDerivedType::PtrAuthData Rhs) {
  return Lhs.RawData == Rhs.RawData;
}

/// Return true if the two values compare unequal.
/// \param Lhs Left-hand pointer-auth data.
/// \param Rhs Right-hand pointer-auth data.
/// \return true if the two values compare unequal.
inline bool operator!=(DIDerivedType::PtrAuthData Lhs,
                       DIDerivedType::PtrAuthData Rhs) {
  return !(Lhs == Rhs);
}

/// Subrange type.  This is somewhat similar to DISubrange, but it
/// is also a DIType.
class DISubrangeType : public DIType {
public:
  /// Pointer-union type for a subrange bound.
  typedef PointerUnion<ConstantInt *, DIVariable *, DIExpression *,
                       DIDerivedType *>
      BoundType;

private:
  friend class LLVMContextImpl;
  friend class MDNode;

  static constexpr unsigned MY_FIRST_OPERAND = DIType::N_OPERANDS;

  /// DI Subrange Type.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Line Source line number.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Flags Flags bitfield.
  /// \param Ops Operand list, or expression opcodes.
  DISubrangeType(LLVMContext &C, StorageType Storage, unsigned Line,
                 uint32_t AlignInBits, DIFlags Flags, ArrayRef<Metadata *> Ops);

  /// Destroy this DISubrangeType.
  ~DISubrangeType() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Flags Flags bitfield.
  /// \param BaseType Underlying or base type.
  /// \param LowerBound Lower bound of a subrange.
  /// \param UpperBound Upper bound of a subrange.
  /// \param Stride Stride of a subrange.
  /// \param Bias Bias applied to a subrange type.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DISubrangeType *
  getImpl(LLVMContext &Context, StringRef Name, DIFile *File, unsigned Line,
          DIScope *Scope, uint64_t SizeInBits, uint32_t AlignInBits,
          DIFlags Flags, DIType *BaseType, Metadata *LowerBound,
          Metadata *UpperBound, Metadata *Stride, Metadata *Bias,
          StorageType Storage, bool ShouldCreate = true) {
    auto *SizeInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), SizeInBits));
    return getImpl(Context, getCanonicalMDString(Context, Name), File, Line,
                   Scope, SizeInBitsNode, AlignInBits, Flags, BaseType,
                   LowerBound, UpperBound, Stride, Bias, Storage, ShouldCreate);
  }

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Flags Flags bitfield.
  /// \param BaseType Underlying or base type.
  /// \param LowerBound Lower bound of a subrange.
  /// \param UpperBound Upper bound of a subrange.
  /// \param Stride Stride of a subrange.
  /// \param Bias Bias applied to a subrange type.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DISubrangeType *
  getImpl(LLVMContext &Context, MDString *Name, Metadata *File, unsigned Line,
          Metadata *Scope, Metadata *SizeInBits, uint32_t AlignInBits,
          DIFlags Flags, Metadata *BaseType, Metadata *LowerBound,
          Metadata *UpperBound, Metadata *Stride, Metadata *Bias,
          StorageType Storage, bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDISubrangeType cloneImpl() const {
    return getTemporary(getContext(), getRawName(), getFile(), getLine(),
                        getScope(), getRawSizeInBits(), getAlignInBits(),
                        getFlags(), getBaseType(), getRawLowerBound(),
                        getRawUpperBound(), getRawStride(), getRawBias());
  }

  /// Convert a raw metadata bound operand into a BoundType.
  /// \param IN Raw metadata representing the bound.
  /// \return convert a raw metadata bound operand into a BoundType.
  LLVM_ABI BoundType convertRawToBound(Metadata *IN) const;

public:
  /// Get or create a DISubrangeType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Flags Flags bitfield.
  /// \param BaseType Underlying or base type.
  /// \param LowerBound Lower bound of a subrange.
  /// \param UpperBound Upper bound of a subrange.
  /// \param Stride Stride of a subrange.
  /// \param Bias Bias applied to a subrange type.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DISubrangeType,
                    (MDString * Name, Metadata *File, unsigned Line,
                     Metadata *Scope, Metadata *SizeInBits,
                     uint32_t AlignInBits, DIFlags Flags, Metadata *BaseType,
                     Metadata *LowerBound, Metadata *UpperBound,
                     Metadata *Stride, Metadata *Bias),
                    (Name, File, Line, Scope, SizeInBits, AlignInBits, Flags,
                     BaseType, LowerBound, UpperBound, Stride, Bias))
  /// Get or create a DISubrangeType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Flags Flags bitfield.
  /// \param BaseType Underlying or base type.
  /// \param LowerBound Lower bound of a subrange.
  /// \param UpperBound Upper bound of a subrange.
  /// \param Stride Stride of a subrange.
  /// \param Bias Bias applied to a subrange type.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DISubrangeType,
                    (StringRef Name, DIFile *File, unsigned Line,
                     DIScope *Scope, uint64_t SizeInBits, uint32_t AlignInBits,
                     DIFlags Flags, DIType *BaseType, Metadata *LowerBound,
                     Metadata *UpperBound, Metadata *Stride, Metadata *Bias),
                    (Name, File, Line, Scope, SizeInBits, AlignInBits, Flags,
                     BaseType, LowerBound, UpperBound, Stride, Bias))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDISubrangeType clone() const { return cloneImpl(); }

  /// Get the base type this is derived from.
  /// \return get the base type this is derived from.
  DIType *getBaseType() const { return cast_or_null<DIType>(getRawBaseType()); }
  /// Return the raw base type operand.
  /// \return The raw base type operand.
  Metadata *getRawBaseType() const { return getOperand(MY_FIRST_OPERAND); }

  /// Return the raw lower bound operand.
  /// \return The raw lower bound operand.
  Metadata *getRawLowerBound() const {
    return getOperand(MY_FIRST_OPERAND + 1).get();
  }

  /// Return the raw upper bound operand.
  /// \return The raw upper bound operand.
  Metadata *getRawUpperBound() const {
    return getOperand(MY_FIRST_OPERAND + 2).get();
  }

  /// Return the raw stride operand.
  /// \return The raw stride operand.
  Metadata *getRawStride() const {
    return getOperand(MY_FIRST_OPERAND + 3).get();
  }

  /// Return the raw bias operand.
  /// \return The raw bias operand.
  Metadata *getRawBias() const {
    return getOperand(MY_FIRST_OPERAND + 4).get();
  }

  /// Return the lower bound.
  /// \return The lower bound.
  BoundType getLowerBound() const {
    return convertRawToBound(getRawLowerBound());
  }

  /// Return the upper bound.
  /// \return The upper bound.
  BoundType getUpperBound() const {
    return convertRawToBound(getRawUpperBound());
  }

  /// Return the stride.
  /// \return The stride.
  BoundType getStride() const { return convertRawToBound(getRawStride()); }

  /// Return the bias.
  /// \return The bias.
  BoundType getBias() const { return convertRawToBound(getRawBias()); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DISubrangeTypeKind;
  }
};

/// Composite types.
///
/// TODO: Detach from DerivedTypeBase (split out MDEnumType?).
/// TODO: Create a custom, unrelated node for DW_TAG_array_type.
class DICompositeType : public DIType {
  friend class LLVMContextImpl;
  friend class MDNode;

  static constexpr unsigned MY_FIRST_OPERAND = DIType::N_OPERANDS;

  unsigned RuntimeLang;
  std::optional<uint32_t> EnumKind;

  DICompositeType(LLVMContext &C, StorageType Storage, unsigned Tag,
                  unsigned Line, unsigned RuntimeLang, uint32_t AlignInBits,
                  uint32_t NumExtraInhabitants,
                  std::optional<uint32_t> EnumKind, DIFlags Flags,
                  ArrayRef<Metadata *> Ops)
      : DIType(C, DICompositeTypeKind, Storage, Tag, Line, AlignInBits,
               NumExtraInhabitants, Flags, Ops),
        RuntimeLang(RuntimeLang), EnumKind(EnumKind) {}
  /// Destroy this DICompositeType.
  ~DICompositeType() = default;

  /// Change fields in place.
  /// \param Tag DWARF tag for the node.
  /// \param Line Source line number.
  /// \param RuntimeLang Runtime language code.
  /// \param AlignInBits ABI alignment in bits.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param EnumKind Enumerated type kind.
  /// \param Flags Flags bitfield.
  void mutate(unsigned Tag, unsigned Line, unsigned RuntimeLang,
              uint32_t AlignInBits, uint32_t NumExtraInhabitants,
              std::optional<uint32_t> EnumKind, DIFlags Flags) {
    assert(isDistinct() && "Only distinct nodes can mutate");
    assert(getRawIdentifier() && "Only ODR-uniqued nodes should mutate");
    this->RuntimeLang = RuntimeLang;
    this->EnumKind = EnumKind;
    /// Mutate common DIType fields in place.
    /// \param Tag DWARF tag for the node.
    /// \param Line Source line number.
    /// \param AlignInBits ABI alignment in bits.
    /// \param NumExtraInhabitants Number of extra inhabitants for the type.
    /// \param Flags Flags bitfield.
    DIType::mutate(Tag, Line, AlignInBits, NumExtraInhabitants, Flags);
  }

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param Specification Type specification this completes.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param Flags Flags bitfield.
  /// \param Elements Elements array or expression element words.
  /// \param RuntimeLang Runtime language code.
  /// \param EnumKind Enumerated type kind.
  /// \param VTableHolder Type that holds the vtable.
  /// \param TemplateParams Template parameter list.
  /// \param Identifier ODR type identifier string.
  /// \param Discriminator Encoded discriminator value.
  /// \param DataLocation Fortran data-location expression or variable.
  /// \param Associated Fortran associated expression or variable.
  /// \param Allocated Fortran allocated expression or variable.
  /// \param Rank Fortran rank constant or expression.
  /// \param Annotations Annotation metadata tuple.
  /// \param BitStride Bit stride for the composite type.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DICompositeType *
  getImpl(LLVMContext &Context, unsigned Tag, StringRef Name, Metadata *File,
          unsigned Line, DIScope *Scope, DIType *BaseType, uint64_t SizeInBits,
          uint32_t AlignInBits, uint64_t OffsetInBits, DIType *Specification,
          uint32_t NumExtraInhabitants, DIFlags Flags, DINodeArray Elements,
          unsigned RuntimeLang, std::optional<uint32_t> EnumKind,
          DIType *VTableHolder, DITemplateParameterArray TemplateParams,
          StringRef Identifier, DIDerivedType *Discriminator,
          Metadata *DataLocation, Metadata *Associated, Metadata *Allocated,
          Metadata *Rank, DINodeArray Annotations, Metadata *BitStride,
          StorageType Storage, bool ShouldCreate = true) {
    auto *SizeInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), SizeInBits));
    auto *OffsetInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), OffsetInBits));
    return getImpl(Context, Tag, getCanonicalMDString(Context, Name), File,
                   Line, Scope, BaseType, SizeInBitsNode, AlignInBits,
                   OffsetInBitsNode, Flags, Elements.get(), RuntimeLang,
                   EnumKind, VTableHolder, TemplateParams.get(),
                   getCanonicalMDString(Context, Identifier), Discriminator,
                   DataLocation, Associated, Allocated, Rank, Annotations.get(),
                   Specification, NumExtraInhabitants, BitStride, Storage,
                   ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param Flags Flags bitfield.
  /// \param Elements Elements array or expression element words.
  /// \param RuntimeLang Runtime language code.
  /// \param EnumKind Enumerated type kind.
  /// \param VTableHolder Type that holds the vtable.
  /// \param TemplateParams Template parameter list.
  /// \param Identifier ODR type identifier string.
  /// \param Discriminator Encoded discriminator value.
  /// \param DataLocation Fortran data-location expression or variable.
  /// \param Associated Fortran associated expression or variable.
  /// \param Allocated Fortran allocated expression or variable.
  /// \param Rank Fortran rank constant or expression.
  /// \param Annotations Annotation metadata tuple.
  /// \param Specification Type specification this completes.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param BitStride Bit stride for the composite type.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DICompositeType *
  getImpl(LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *File,
          unsigned Line, Metadata *Scope, Metadata *BaseType,
          uint64_t SizeInBits, uint32_t AlignInBits, uint64_t OffsetInBits,
          DIFlags Flags, Metadata *Elements, unsigned RuntimeLang,
          std::optional<uint32_t> EnumKind, Metadata *VTableHolder,
          Metadata *TemplateParams, MDString *Identifier,
          Metadata *Discriminator, Metadata *DataLocation, Metadata *Associated,
          Metadata *Allocated, Metadata *Rank, Metadata *Annotations,
          Metadata *Specification, uint32_t NumExtraInhabitants,
          Metadata *BitStride, StorageType Storage, bool ShouldCreate = true) {
    auto *SizeInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), SizeInBits));
    auto *OffsetInBitsNode = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Context), OffsetInBits));
    return getImpl(Context, Tag, Name, File, Line, Scope, BaseType,
                   SizeInBitsNode, AlignInBits, OffsetInBitsNode, Flags,
                   Elements, RuntimeLang, EnumKind, VTableHolder,
                   TemplateParams, Identifier, Discriminator, DataLocation,
                   Associated, Allocated, Rank, Annotations, Specification,
                   NumExtraInhabitants, BitStride, Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param Specification Type specification this completes.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param Flags Flags bitfield.
  /// \param Elements Elements array or expression element words.
  /// \param RuntimeLang Runtime language code.
  /// \param EnumKind Enumerated type kind.
  /// \param VTableHolder Type that holds the vtable.
  /// \param TemplateParams Template parameter list.
  /// \param Identifier ODR type identifier string.
  /// \param Discriminator Encoded discriminator value.
  /// \param DataLocation Fortran data-location expression or variable.
  /// \param Associated Fortran associated expression or variable.
  /// \param Allocated Fortran allocated expression or variable.
  /// \param Rank Fortran rank constant or expression.
  /// \param Annotations Annotation metadata tuple.
  /// \param BitStride Bit stride for the composite type.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DICompositeType *
  getImpl(LLVMContext &Context, unsigned Tag, StringRef Name, Metadata *File,
          unsigned Line, DIScope *Scope, DIType *BaseType, Metadata *SizeInBits,
          uint32_t AlignInBits, Metadata *OffsetInBits, DIType *Specification,
          uint32_t NumExtraInhabitants, DIFlags Flags, DINodeArray Elements,
          unsigned RuntimeLang, std::optional<uint32_t> EnumKind,
          DIType *VTableHolder, DITemplateParameterArray TemplateParams,
          StringRef Identifier, DIDerivedType *Discriminator,
          Metadata *DataLocation, Metadata *Associated, Metadata *Allocated,
          Metadata *Rank, DINodeArray Annotations, Metadata *BitStride,
          StorageType Storage, bool ShouldCreate = true) {
    return getImpl(
        Context, Tag, getCanonicalMDString(Context, Name), File, Line, Scope,
        BaseType, SizeInBits, AlignInBits, OffsetInBits, Flags, Elements.get(),
        RuntimeLang, EnumKind, VTableHolder, TemplateParams.get(),
        getCanonicalMDString(Context, Identifier), Discriminator, DataLocation,
        Associated, Allocated, Rank, Annotations.get(), Specification,
        NumExtraInhabitants, BitStride, Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param Flags Flags bitfield.
  /// \param Elements Elements array or expression element words.
  /// \param RuntimeLang Runtime language code.
  /// \param EnumKind Enumerated type kind.
  /// \param VTableHolder Type that holds the vtable.
  /// \param TemplateParams Template parameter list.
  /// \param Identifier ODR type identifier string.
  /// \param Discriminator Encoded discriminator value.
  /// \param DataLocation Fortran data-location expression or variable.
  /// \param Associated Fortran associated expression or variable.
  /// \param Allocated Fortran allocated expression or variable.
  /// \param Rank Fortran rank constant or expression.
  /// \param Annotations Annotation metadata tuple.
  /// \param Specification Type specification this completes.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param BitStride Bit stride for the composite type.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DICompositeType *
  getImpl(LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *File,
          unsigned Line, Metadata *Scope, Metadata *BaseType,
          Metadata *SizeInBits, uint32_t AlignInBits, Metadata *OffsetInBits,
          DIFlags Flags, Metadata *Elements, unsigned RuntimeLang,
          std::optional<uint32_t> EnumKind, Metadata *VTableHolder,
          Metadata *TemplateParams, MDString *Identifier,
          Metadata *Discriminator, Metadata *DataLocation, Metadata *Associated,
          Metadata *Allocated, Metadata *Rank, Metadata *Annotations,
          Metadata *Specification, uint32_t NumExtraInhabitants,
          Metadata *BitStride, StorageType Storage, bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDICompositeType cloneImpl() const {
    return getTemporary(
        getContext(), getTag(), getRawName(), getFile(), getLine(), getScope(),
        getBaseType(), getRawSizeInBits(), getAlignInBits(),
        getRawOffsetInBits(), getFlags(), getRawElements(), getRuntimeLang(),
        getEnumKind(), getVTableHolder(), getRawTemplateParams(),
        getRawIdentifier(), getDiscriminator(), getRawDataLocation(),
        getRawAssociated(), getRawAllocated(), getRawRank(),
        getRawAnnotations(), getSpecification(), getNumExtraInhabitants(),
        getRawBitStride());
  }

public:
  /// Get or create a DICompositeType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param Flags Flags bitfield.
  /// \param Elements Elements array or expression element words.
  /// \param RuntimeLang Runtime language code.
  /// \param EnumKind Enumerated type kind.
  /// \param VTableHolder Type that holds the vtable.
  /// \param TemplateParams Template parameter list.
  /// \param Identifier ODR type identifier string.
  /// \param Discriminator Encoded discriminator value.
  /// \param DataLocation Fortran data-location expression or variable.
  /// \param Associated Fortran associated expression or variable.
  /// \param Allocated Fortran allocated expression or variable.
  /// \param Rank Fortran rank constant or expression.
  /// \param Annotations Annotation metadata tuple.
  /// \param Specification Type specification this completes.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param BitStride Bit stride for the composite type.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(
      DICompositeType,
      (unsigned Tag, StringRef Name, DIFile *File, unsigned Line,
       DIScope *Scope, DIType *BaseType, uint64_t SizeInBits,
       uint32_t AlignInBits, uint64_t OffsetInBits, DIFlags Flags,
       DINodeArray Elements, unsigned RuntimeLang,
       std::optional<uint32_t> EnumKind, DIType *VTableHolder,
       DITemplateParameterArray TemplateParams = nullptr,
       StringRef Identifier = "", DIDerivedType *Discriminator = nullptr,
       Metadata *DataLocation = nullptr, Metadata *Associated = nullptr,
       Metadata *Allocated = nullptr, Metadata *Rank = nullptr,
       DINodeArray Annotations = nullptr, DIType *Specification = nullptr,
       uint32_t NumExtraInhabitants = 0, Metadata *BitStride = nullptr),
      (Tag, Name, File, Line, Scope, BaseType, SizeInBits, AlignInBits,
       OffsetInBits, Specification, NumExtraInhabitants, Flags, Elements,
       RuntimeLang, EnumKind, VTableHolder, TemplateParams, Identifier,
       Discriminator, DataLocation, Associated, Allocated, Rank, Annotations,
       BitStride))
  /// Get or create a DICompositeType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param Flags Flags bitfield.
  /// \param Elements Elements array or expression element words.
  /// \param RuntimeLang Runtime language code.
  /// \param EnumKind Enumerated type kind.
  /// \param VTableHolder Type that holds the vtable.
  /// \param TemplateParams Template parameter list.
  /// \param Identifier ODR type identifier string.
  /// \param Discriminator Encoded discriminator value.
  /// \param DataLocation Fortran data-location expression or variable.
  /// \param Associated Fortran associated expression or variable.
  /// \param Allocated Fortran allocated expression or variable.
  /// \param Rank Fortran rank constant or expression.
  /// \param Annotations Annotation metadata tuple.
  /// \param Specification Type specification this completes.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param BitStride Bit stride for the composite type.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(
      DICompositeType,
      (unsigned Tag, MDString *Name, Metadata *File, unsigned Line,
       Metadata *Scope, Metadata *BaseType, uint64_t SizeInBits,
       uint32_t AlignInBits, uint64_t OffsetInBits, DIFlags Flags,
       Metadata *Elements, unsigned RuntimeLang,
       std::optional<uint32_t> EnumKind, Metadata *VTableHolder,
       Metadata *TemplateParams = nullptr, MDString *Identifier = nullptr,
       Metadata *Discriminator = nullptr, Metadata *DataLocation = nullptr,
       Metadata *Associated = nullptr, Metadata *Allocated = nullptr,
       Metadata *Rank = nullptr, Metadata *Annotations = nullptr,
       Metadata *Specification = nullptr, uint32_t NumExtraInhabitants = 0,
       Metadata *BitStride = nullptr),
      (Tag, Name, File, Line, Scope, BaseType, SizeInBits, AlignInBits,
       OffsetInBits, Flags, Elements, RuntimeLang, EnumKind, VTableHolder,
       TemplateParams, Identifier, Discriminator, DataLocation, Associated,
       Allocated, Rank, Annotations, Specification, NumExtraInhabitants,
       BitStride))
  /// Get or create a DICompositeType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param Flags Flags bitfield.
  /// \param Elements Elements array or expression element words.
  /// \param RuntimeLang Runtime language code.
  /// \param EnumKind Enumerated type kind.
  /// \param VTableHolder Type that holds the vtable.
  /// \param TemplateParams Template parameter list.
  /// \param Identifier ODR type identifier string.
  /// \param Discriminator Encoded discriminator value.
  /// \param DataLocation Fortran data-location expression or variable.
  /// \param Associated Fortran associated expression or variable.
  /// \param Allocated Fortran allocated expression or variable.
  /// \param Rank Fortran rank constant or expression.
  /// \param Annotations Annotation metadata tuple.
  /// \param Specification Type specification this completes.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param BitStride Bit stride for the composite type.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(
      DICompositeType,
      (unsigned Tag, StringRef Name, DIFile *File, unsigned Line,
       DIScope *Scope, DIType *BaseType, Metadata *SizeInBits,
       uint32_t AlignInBits, Metadata *OffsetInBits, DIFlags Flags,
       DINodeArray Elements, unsigned RuntimeLang,
       std::optional<uint32_t> EnumKind, DIType *VTableHolder,
       DITemplateParameterArray TemplateParams = nullptr,
       StringRef Identifier = "", DIDerivedType *Discriminator = nullptr,
       Metadata *DataLocation = nullptr, Metadata *Associated = nullptr,
       Metadata *Allocated = nullptr, Metadata *Rank = nullptr,
       DINodeArray Annotations = nullptr, DIType *Specification = nullptr,
       uint32_t NumExtraInhabitants = 0, Metadata *BitStride = nullptr),
      (Tag, Name, File, Line, Scope, BaseType, SizeInBits, AlignInBits,
       OffsetInBits, Specification, NumExtraInhabitants, Flags, Elements,
       RuntimeLang, EnumKind, VTableHolder, TemplateParams, Identifier,
       Discriminator, DataLocation, Associated, Allocated, Rank, Annotations,
       BitStride))
  /// Get or create a DICompositeType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param Flags Flags bitfield.
  /// \param Elements Elements array or expression element words.
  /// \param RuntimeLang Runtime language code.
  /// \param EnumKind Enumerated type kind.
  /// \param VTableHolder Type that holds the vtable.
  /// \param TemplateParams Template parameter list.
  /// \param Identifier ODR type identifier string.
  /// \param Discriminator Encoded discriminator value.
  /// \param DataLocation Fortran data-location expression or variable.
  /// \param Associated Fortran associated expression or variable.
  /// \param Allocated Fortran allocated expression or variable.
  /// \param Rank Fortran rank constant or expression.
  /// \param Annotations Annotation metadata tuple.
  /// \param Specification Type specification this completes.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param BitStride Bit stride for the composite type.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(
      DICompositeType,
      (unsigned Tag, MDString *Name, Metadata *File, unsigned Line,
       Metadata *Scope, Metadata *BaseType, Metadata *SizeInBits,
       uint32_t AlignInBits, Metadata *OffsetInBits, DIFlags Flags,
       Metadata *Elements, unsigned RuntimeLang,
       std::optional<uint32_t> EnumKind, Metadata *VTableHolder,
       Metadata *TemplateParams = nullptr, MDString *Identifier = nullptr,
       Metadata *Discriminator = nullptr, Metadata *DataLocation = nullptr,
       Metadata *Associated = nullptr, Metadata *Allocated = nullptr,
       Metadata *Rank = nullptr, Metadata *Annotations = nullptr,
       Metadata *Specification = nullptr, uint32_t NumExtraInhabitants = 0,
       Metadata *BitStride = nullptr),
      (Tag, Name, File, Line, Scope, BaseType, SizeInBits, AlignInBits,
       OffsetInBits, Flags, Elements, RuntimeLang, EnumKind, VTableHolder,
       TemplateParams, Identifier, Discriminator, DataLocation, Associated,
       Allocated, Rank, Annotations, Specification, NumExtraInhabitants,
       BitStride))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDICompositeType clone() const { return cloneImpl(); }

  /// Get a DICompositeType with the given ODR identifier.
  ///
  /// If \a LLVMContext::isODRUniquingDebugTypes(), gets the mapped
  /// DICompositeType for the given ODR \c Identifier.  If none exists, creates
  /// a new node.
  ///
  /// Else, returns \c nullptr.
  /// \param Context LLVM context that owns the metadata.
  /// \param Identifier ODR type identifier string.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param Specification Type specification this completes.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param Flags Flags bitfield.
  /// \param Elements Elements array or expression element words.
  /// \param RuntimeLang Runtime language code.
  /// \param EnumKind Enumerated type kind.
  /// \param VTableHolder Type that holds the vtable.
  /// \param TemplateParams Template parameter list.
  /// \param Discriminator Encoded discriminator value.
  /// \param DataLocation Fortran data-location expression or variable.
  /// \param Associated Fortran associated expression or variable.
  /// \param Allocated Fortran allocated expression or variable.
  /// \param Rank Fortran rank constant or expression.
  /// \param Annotations Annotation metadata tuple.
  /// \param BitStride Bit stride for the composite type.
  /// \return get a DICompositeType with the given ODR identifier.
  LLVM_ABI static DICompositeType *
  getODRType(LLVMContext &Context, MDString &Identifier, unsigned Tag,
             MDString *Name, Metadata *File, unsigned Line, Metadata *Scope,
             Metadata *BaseType, Metadata *SizeInBits, uint32_t AlignInBits,
             Metadata *OffsetInBits, Metadata *Specification,
             uint32_t NumExtraInhabitants, DIFlags Flags, Metadata *Elements,
             unsigned RuntimeLang, std::optional<uint32_t> EnumKind,
             Metadata *VTableHolder, Metadata *TemplateParams,
             Metadata *Discriminator, Metadata *DataLocation,
             Metadata *Associated, Metadata *Allocated, Metadata *Rank,
             Metadata *Annotations, Metadata *BitStride);
  /// Return the odr type if exists.
  /// \param Context LLVM context that owns the metadata.
  /// \param Identifier ODR type identifier string.
  /// \return The odr type if exists.
  LLVM_ABI static DICompositeType *getODRTypeIfExists(LLVMContext &Context,
                                                      MDString &Identifier);

  /// Build a DICompositeType with the given ODR identifier.
  ///
  /// Looks up the mapped DICompositeType for the given ODR \c Identifier.  If
  /// it doesn't exist, creates a new one.  If it does exist and \a
  /// isForwardDecl(), and the new arguments would be a definition, mutates the
  /// the type in place.  In either case, returns the type.
  ///
  /// If not \a LLVMContext::isODRUniquingDebugTypes(), this function returns
  /// nullptr.
  /// \param Context LLVM context that owns the metadata.
  /// \param Identifier ODR type identifier string.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Scope Parent lexical or type scope.
  /// \param BaseType Underlying or base type.
  /// \param SizeInBits Size of the type in bits.
  /// \param AlignInBits ABI alignment in bits.
  /// \param OffsetInBits Bit offset within the parent or fragment.
  /// \param Specification Type specification this completes.
  /// \param NumExtraInhabitants Number of extra inhabitants for the type.
  /// \param Flags Flags bitfield.
  /// \param Elements Elements array or expression element words.
  /// \param RuntimeLang Runtime language code.
  /// \param EnumKind Enumerated type kind.
  /// \param VTableHolder Type that holds the vtable.
  /// \param TemplateParams Template parameter list.
  /// \param Discriminator Encoded discriminator value.
  /// \param DataLocation Fortran data-location expression or variable.
  /// \param Associated Fortran associated expression or variable.
  /// \param Allocated Fortran allocated expression or variable.
  /// \param Rank Fortran rank constant or expression.
  /// \param Annotations Annotation metadata tuple.
  /// \param BitStride Bit stride for the composite type.
  /// \return The DICompositeType for the ODR identifier.
  LLVM_ABI static DICompositeType *
  buildODRType(LLVMContext &Context, MDString &Identifier, unsigned Tag,
               MDString *Name, Metadata *File, unsigned Line, Metadata *Scope,
               Metadata *BaseType, Metadata *SizeInBits, uint32_t AlignInBits,
               Metadata *OffsetInBits, Metadata *Specification,
               uint32_t NumExtraInhabitants, DIFlags Flags, Metadata *Elements,
               unsigned RuntimeLang, std::optional<uint32_t> EnumKind,
               Metadata *VTableHolder, Metadata *TemplateParams,
               Metadata *Discriminator, Metadata *DataLocation,
               Metadata *Associated, Metadata *Allocated, Metadata *Rank,
               Metadata *Annotations, Metadata *BitStride);

  /// Return the base type.
  /// \return The base type.
  DIType *getBaseType() const { return cast_or_null<DIType>(getRawBaseType()); }
  /// Return the elements.
  /// \return The elements.
  DINodeArray getElements() const {
    return cast_or_null<MDTuple>(getRawElements());
  }
  /// Return the v table holder.
  /// \return The v table holder.
  DIType *getVTableHolder() const {
    return cast_or_null<DIType>(getRawVTableHolder());
  }
  /// Return the template params.
  /// \return The template params.
  DITemplateParameterArray getTemplateParams() const {
    return cast_or_null<MDTuple>(getRawTemplateParams());
  }
  /// Return the identifier.
  /// \return The identifier.
  StringRef getIdentifier() const {
    return getStringOperand(MY_FIRST_OPERAND + 4);
  }
  /// Return the runtime lang.
  /// \return The runtime lang.
  unsigned getRuntimeLang() const { return RuntimeLang; }
  /// Return the enum kind.
  /// \return The enum kind.
  std::optional<uint32_t> getEnumKind() const { return EnumKind; }

  /// Return the raw base type operand.
  /// \return The raw base type operand.
  Metadata *getRawBaseType() const { return getOperand(MY_FIRST_OPERAND); }
  /// Return the raw elements operand.
  /// \return The raw elements operand.
  Metadata *getRawElements() const { return getOperand(MY_FIRST_OPERAND + 1); }
  /// Return the raw v table holder operand.
  /// \return The raw v table holder operand.
  Metadata *getRawVTableHolder() const {
    return getOperand(MY_FIRST_OPERAND + 2);
  }
  /// Return the raw template params operand.
  /// \return The raw template params operand.
  Metadata *getRawTemplateParams() const {
    return getOperand(MY_FIRST_OPERAND + 3);
  }
  /// Return the raw identifier operand.
  /// \return The raw identifier operand.
  MDString *getRawIdentifier() const {
    return getOperandAs<MDString>(MY_FIRST_OPERAND + 4);
  }
  /// Return the raw discriminator operand.
  /// \return The raw discriminator operand.
  Metadata *getRawDiscriminator() const {
    return getOperand(MY_FIRST_OPERAND + 5);
  }
  /// Return the discriminator.
  /// \return The discriminator.
  DIDerivedType *getDiscriminator() const {
    return getOperandAs<DIDerivedType>(MY_FIRST_OPERAND + 5);
  }
  /// Return the raw data location operand.
  /// \return The raw data location operand.
  Metadata *getRawDataLocation() const {
    return getOperand(MY_FIRST_OPERAND + 6);
  }
  /// Return the data location.
  /// \return The data location.
  DIVariable *getDataLocation() const {
    return dyn_cast_or_null<DIVariable>(getRawDataLocation());
  }
  /// Return the data location exp.
  /// \return The data location exp.
  DIExpression *getDataLocationExp() const {
    return dyn_cast_or_null<DIExpression>(getRawDataLocation());
  }
  /// Return the raw associated operand.
  /// \return The raw associated operand.
  Metadata *getRawAssociated() const {
    return getOperand(MY_FIRST_OPERAND + 7);
  }
  /// Return the associated.
  /// \return The associated.
  DIVariable *getAssociated() const {
    return dyn_cast_or_null<DIVariable>(getRawAssociated());
  }
  /// Return the associated exp.
  /// \return The associated exp.
  DIExpression *getAssociatedExp() const {
    return dyn_cast_or_null<DIExpression>(getRawAssociated());
  }
  /// Return the raw allocated operand.
  /// \return The raw allocated operand.
  Metadata *getRawAllocated() const { return getOperand(MY_FIRST_OPERAND + 8); }
  /// Return the allocated.
  /// \return The allocated.
  DIVariable *getAllocated() const {
    return dyn_cast_or_null<DIVariable>(getRawAllocated());
  }
  /// Return the allocated exp.
  /// \return The allocated exp.
  DIExpression *getAllocatedExp() const {
    return dyn_cast_or_null<DIExpression>(getRawAllocated());
  }
  /// Return the raw rank operand.
  /// \return The raw rank operand.
  Metadata *getRawRank() const { return getOperand(MY_FIRST_OPERAND + 9); }
  /// Return the rank const.
  /// \return The rank const.
  ConstantInt *getRankConst() const {
    if (auto *MD = dyn_cast_or_null<ConstantAsMetadata>(getRawRank()))
      return dyn_cast_or_null<ConstantInt>(MD->getValue());
    return nullptr;
  }
  /// Return the rank exp.
  /// \return The rank exp.
  DIExpression *getRankExp() const {
    return dyn_cast_or_null<DIExpression>(getRawRank());
  }

  /// Return the raw annotations operand.
  /// \return The raw annotations operand.
  Metadata *getRawAnnotations() const {
    return getOperand(MY_FIRST_OPERAND + 10);
  }
  /// Return the annotations.
  /// \return The annotations.
  DINodeArray getAnnotations() const {
    return cast_or_null<MDTuple>(getRawAnnotations());
  }

  /// Return the raw specification operand.
  /// \return The raw specification operand.
  Metadata *getRawSpecification() const {
    return getOperand(MY_FIRST_OPERAND + 11);
  }
  /// Return the specification.
  /// \return The specification.
  DIType *getSpecification() const {
    return cast_or_null<DIType>(getRawSpecification());
  }

  /// Return true if this is name simplified.
  /// \return true if this is name simplified.
  bool isNameSimplified() const { return getFlags() & FlagNameIsSimplified; }

  /// Return the raw bit stride operand.
  /// \return The raw bit stride operand.
  Metadata *getRawBitStride() const {
    return getOperand(MY_FIRST_OPERAND + 12);
  }
  /// Return the bit stride const.
  /// \return The bit stride const.
  ConstantInt *getBitStrideConst() const {
    if (auto *MD = dyn_cast_or_null<ConstantAsMetadata>(getRawBitStride()))
      return dyn_cast_or_null<ConstantInt>(MD->getValue());
    return nullptr;
  }

  /// Replace operands.
  ///
  /// If this \a isUniqued() and not \a isResolved(), on a uniquing collision
  /// this will be RAUW'ed and deleted.  Use a \a TrackingMDRef to keep track
  /// of its movement if necessary.
  /// @{
  /// \param Elements Elements array or expression element words.
  void replaceElements(DINodeArray Elements) {
#ifndef NDEBUG
    for (DINode *Op : getElements())
      assert(is_contained(Elements->operands(), Op) &&
             "Lost a member during member list replacement");
#endif
    replaceOperandWith(MY_FIRST_OPERAND + 1, Elements.get());
  }

  /// Replace the v table holder.
  /// \param VTableHolder Type that holds the vtable.
  void replaceVTableHolder(DIType *VTableHolder) {
    /// Replace the operand with.
    /// \param VTableHolder Type that holds the vtable.
    replaceOperandWith(MY_FIRST_OPERAND + 2, VTableHolder);
  }

  /// Replace the template params.
  /// \param TemplateParams Template parameter list.
  void replaceTemplateParams(DITemplateParameterArray TemplateParams) {
    replaceOperandWith(MY_FIRST_OPERAND + 3, TemplateParams.get());
  }
  /// @}

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DICompositeTypeKind;
  }
};

/// Type array for a subprogram.
///
/// TODO: Fold the array of types in directly as operands.
class DISubroutineType : public DIType {
  friend class LLVMContextImpl;
  friend class MDNode;

  static constexpr unsigned MY_FIRST_OPERAND = DIType::N_OPERANDS;

  /// The calling convention used with DW_AT_calling_convention. Actually of
  /// type dwarf::CallingConvention.
  uint8_t CC;

  /// DI Subroutine Type.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Flags Flags bitfield.
  /// \param CC DWARF calling convention.
  /// \param Ops Operand list, or expression opcodes.
  DISubroutineType(LLVMContext &C, StorageType Storage, DIFlags Flags,
                   uint8_t CC, ArrayRef<Metadata *> Ops);
  /// Destroy this DISubroutineType.
  ~DISubroutineType() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Flags Flags bitfield.
  /// \param CC DWARF calling convention.
  /// \param TypeArray Subroutine parameter and return type array.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DISubroutineType *getImpl(LLVMContext &Context, DIFlags Flags,
                                   uint8_t CC, DITypeArray TypeArray,
                                   StorageType Storage,
                                   bool ShouldCreate = true) {
    return getImpl(Context, Flags, CC, TypeArray.get(), Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Flags Flags bitfield.
  /// \param CC DWARF calling convention.
  /// \param TypeArray Subroutine parameter and return type array.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DISubroutineType *getImpl(LLVMContext &Context, DIFlags Flags,
                                            uint8_t CC, Metadata *TypeArray,
                                            StorageType Storage,
                                            bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDISubroutineType cloneImpl() const {
    return getTemporary(getContext(), getFlags(), getCC(), getTypeArray());
  }

public:
  /// Get or create a DISubroutineType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Flags Flags bitfield.
  /// \param CC DWARF calling convention.
  /// \param TypeArray Subroutine parameter and return type array.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DISubroutineType,
                    (DIFlags Flags, uint8_t CC, DITypeArray TypeArray),
                    (Flags, CC, TypeArray))
  /// Get or create a DISubroutineType with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Flags Flags bitfield.
  /// \param CC DWARF calling convention.
  /// \param TypeArray Subroutine parameter and return type array.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DISubroutineType,
                    (DIFlags Flags, uint8_t CC, Metadata *TypeArray),
                    (Flags, CC, TypeArray))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDISubroutineType clone() const { return cloneImpl(); }
  // Returns a new temporary DISubroutineType with updated CC
  /// Clone this subroutine type with a different calling convention.
  /// \param CC DWARF calling convention.
  /// \return A temporary clone of this node.
  TempDISubroutineType cloneWithCC(uint8_t CC) const {
    auto NewTy = clone();
    NewTy->CC = CC;
    return NewTy;
  }

  /// Return the cc.
  /// \return The cc.
  uint8_t getCC() const { return CC; }

  /// Return the type array.
  /// \return The type array.
  DITypeArray getTypeArray() const {
    return cast_or_null<MDTuple>(getRawTypeArray());
  }

  /// Return the raw type array operand.
  /// \return The raw type array operand.
  Metadata *getRawTypeArray() const { return getOperand(MY_FIRST_OPERAND); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DISubroutineTypeKind;
  }
};

/// Compile unit.
class DICompileUnit : public DIScope {
  friend class LLVMContextImpl;
  friend class MDNode;

public:
  /// How much debug info a compile unit should emit.
  enum DebugEmissionKind : unsigned {
    /// Emit no debug information.
    NoDebug = 0,
    /// Emit full debug information.
    FullDebug,
    /// Emit line tables only.
    LineTablesOnly,
    /// Emit debug directives only.
    DebugDirectivesOnly,
    /// Last valid emission kind.
    LastEmissionKind = DebugDirectivesOnly
  };

  /// Kind of debug name table to emit.
  enum class DebugNameTableKind : unsigned {
    /// Default name-table format for the target.
    Default = 0,
    /// GNU-style pubnames/pubtypes.
    GNU = 1,
    /// Do not emit a name table.
    None = 2,
    /// Apple accelerator tables.
    Apple = 3,
    /// Last valid name-table kind.
    LastDebugNameTableKind = Apple
  };

  /// Return the emission kind.
  /// \param Str String to parse.
  /// \return The emission kind.
  LLVM_ABI static std::optional<DebugEmissionKind>
  getEmissionKind(StringRef Str);
  /// Return the string name for a debug emission kind.
  /// \param EK The ek.
  /// \return The string name for a debug emission kind.
  LLVM_ABI static const char *emissionKindString(DebugEmissionKind EK);
  /// Return the name table kind.
  /// \param Str String to parse.
  /// \return The name table kind.
  LLVM_ABI static std::optional<DebugNameTableKind>
  getNameTableKind(StringRef Str);
  /// Return the string name for a debug name-table kind.
  /// \param PK The pk.
  /// \return The string name for a debug name-table kind.
  LLVM_ABI static const char *nameTableKindString(DebugNameTableKind PK);

private:
  DISourceLanguageName SourceLanguage;
  unsigned RuntimeVersion;
  uint64_t DWOId;
  unsigned EmissionKind;
  unsigned NameTableKind;
  bool IsOptimized;
  bool SplitDebugInlining;
  bool DebugInfoForProfiling;
  bool RangesBaseAddress;

  /// DI Compile Unit.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param SourceLanguage Source language identity.
  /// \param IsOptimized Whether the compile unit was built optimized.
  /// \param RuntimeVersion The runtime version.
  /// \param EmissionKind Debug info emission kind.
  /// \param DWOId DWO identifier.
  /// \param SplitDebugInlining Whether split DWARF inlining is enabled.
  /// \param DebugInfoForProfiling Whether extra profiling debug info is enabled.
  /// \param NameTableKind Debug name table kind.
  /// \param RangesBaseAddress Whether range lists use a base address.
  /// \param Ops Operand list, or expression opcodes.
  DICompileUnit(LLVMContext &C, StorageType Storage,
                DISourceLanguageName SourceLanguage, bool IsOptimized,
                unsigned RuntimeVersion, unsigned EmissionKind, uint64_t DWOId,
                bool SplitDebugInlining, bool DebugInfoForProfiling,
                unsigned NameTableKind, bool RangesBaseAddress,
                ArrayRef<Metadata *> Ops);
  /// Destroy this DICompileUnit.
  ~DICompileUnit() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param SourceLanguage Source language identity.
  /// \param File Source file metadata.
  /// \param Producer Compiler producer identification string.
  /// \param IsOptimized Whether the compile unit was built optimized.
  /// \param Flags Flags bitfield.
  /// \param RuntimeVersion The runtime version.
  /// \param SplitDebugFilename Split DWARF object filename.
  /// \param EmissionKind Debug info emission kind.
  /// \param EnumTypes The enum types.
  /// \param RetainedTypes Retained types list.
  /// \param GlobalVariables The global variables.
  /// \param ImportedEntities The imported entities.
  /// \param Macros Macros list.
  /// \param DWOId DWO identifier.
  /// \param SplitDebugInlining Whether split DWARF inlining is enabled.
  /// \param DebugInfoForProfiling Whether extra profiling debug info is enabled.
  /// \param NameTableKind Debug name table kind.
  /// \param RangesBaseAddress Whether range lists use a base address.
  /// \param SysRoot SDK or sysroot path string.
  /// \param SDK SDK name string.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DICompileUnit *
  getImpl(LLVMContext &Context, DISourceLanguageName SourceLanguage,
          DIFile *File, StringRef Producer, bool IsOptimized, StringRef Flags,
          unsigned RuntimeVersion, StringRef SplitDebugFilename,
          unsigned EmissionKind, DICompositeTypeArray EnumTypes,
          DIScopeArray RetainedTypes,
          DIGlobalVariableExpressionArray GlobalVariables,
          DIImportedEntityArray ImportedEntities, DIMacroNodeArray Macros,
          uint64_t DWOId, bool SplitDebugInlining, bool DebugInfoForProfiling,
          unsigned NameTableKind, bool RangesBaseAddress, StringRef SysRoot,
          StringRef SDK, StorageType Storage, bool ShouldCreate = true) {
    return getImpl(
        Context, SourceLanguage, File, getCanonicalMDString(Context, Producer),
        IsOptimized, getCanonicalMDString(Context, Flags), RuntimeVersion,
        getCanonicalMDString(Context, SplitDebugFilename), EmissionKind,
        EnumTypes.get(), RetainedTypes.get(), GlobalVariables.get(),
        ImportedEntities.get(), Macros.get(), DWOId, SplitDebugInlining,
        DebugInfoForProfiling, NameTableKind, RangesBaseAddress,
        getCanonicalMDString(Context, SysRoot),
        getCanonicalMDString(Context, SDK), Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param SourceLanguage Source language identity.
  /// \param File Source file metadata.
  /// \param Producer Compiler producer identification string.
  /// \param IsOptimized Whether the compile unit was built optimized.
  /// \param Flags Flags bitfield.
  /// \param RuntimeVersion The runtime version.
  /// \param SplitDebugFilename Split DWARF object filename.
  /// \param EmissionKind Debug info emission kind.
  /// \param EnumTypes The enum types.
  /// \param RetainedTypes Retained types list.
  /// \param GlobalVariables The global variables.
  /// \param ImportedEntities The imported entities.
  /// \param Macros Macros list.
  /// \param DWOId DWO identifier.
  /// \param SplitDebugInlining Whether split DWARF inlining is enabled.
  /// \param DebugInfoForProfiling Whether extra profiling debug info is enabled.
  /// \param NameTableKind Debug name table kind.
  /// \param RangesBaseAddress Whether range lists use a base address.
  /// \param SysRoot SDK or sysroot path string.
  /// \param SDK SDK name string.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DICompileUnit *
  getImpl(LLVMContext &Context, DISourceLanguageName SourceLanguage,
          Metadata *File, MDString *Producer, bool IsOptimized, MDString *Flags,
          unsigned RuntimeVersion, MDString *SplitDebugFilename,
          unsigned EmissionKind, Metadata *EnumTypes, Metadata *RetainedTypes,
          Metadata *GlobalVariables, Metadata *ImportedEntities,
          Metadata *Macros, uint64_t DWOId, bool SplitDebugInlining,
          bool DebugInfoForProfiling, unsigned NameTableKind,
          bool RangesBaseAddress, MDString *SysRoot, MDString *SDK,
          StorageType Storage, bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDICompileUnit cloneImpl() const {
    return getTemporary(
        getContext(), getSourceLanguage(), getFile(), getProducer(),
        isOptimized(), getFlags(), getRuntimeVersion(), getSplitDebugFilename(),
        getEmissionKind(), getEnumTypes(), getRetainedTypes(),
        getGlobalVariables(), getImportedEntities(), getMacros(), DWOId,
        getSplitDebugInlining(), getDebugInfoForProfiling(), getNameTableKind(),
        getRangesBaseAddress(), getSysRoot(), getSDK());
  }

public:
  /// Deleted: DICompileUnit nodes are never uniqued via get().
  static void get() = delete;
  /// Deleted: DICompileUnit nodes are never looked up via getIfExists().
  static void getIfExists() = delete;

  /// Get or create a distinct or temporary DICompileUnit with the given
  /// operands.
  ///
  /// Provides getDistinct and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param SourceLanguage Source language identity.
  /// \param File Source file metadata.
  /// \param Producer Compiler producer identification string.
  /// \param IsOptimized Whether the compile unit was built optimized.
  /// \param Flags Flags bitfield.
  /// \param RuntimeVersion The runtime version.
  /// \param SplitDebugFilename Split DWARF object filename.
  /// \param EmissionKind Debug info emission kind.
  /// \param EnumTypes The enum types.
  /// \param RetainedTypes Retained types list.
  /// \param GlobalVariables The global variables.
  /// \param ImportedEntities The imported entities.
  /// \param Macros Macros list.
  /// \param DWOId DWO identifier.
  /// \param SplitDebugInlining Whether split DWARF inlining is enabled.
  /// \param DebugInfoForProfiling Whether extra profiling debug info is enabled.
  /// \param NameTableKind Debug name table kind.
  /// \param RangesBaseAddress Whether range lists use a base address.
  /// \param SysRoot SDK or sysroot path string.
  /// \param SDK SDK name string.
  /// \return The metadata node (distinct or temporary as requested).
  DEFINE_MDNODE_GET_DISTINCT_TEMPORARY(
      DICompileUnit,
      (DISourceLanguageName SourceLanguage, DIFile *File, StringRef Producer,
       bool IsOptimized, StringRef Flags, unsigned RuntimeVersion,
       StringRef SplitDebugFilename, DebugEmissionKind EmissionKind,
       DICompositeTypeArray EnumTypes, DIScopeArray RetainedTypes,
       DIGlobalVariableExpressionArray GlobalVariables,
       DIImportedEntityArray ImportedEntities, DIMacroNodeArray Macros,
       uint64_t DWOId, bool SplitDebugInlining, bool DebugInfoForProfiling,
       DebugNameTableKind NameTableKind, bool RangesBaseAddress,
       StringRef SysRoot, StringRef SDK),
      (SourceLanguage, File, Producer, IsOptimized, Flags, RuntimeVersion,
       SplitDebugFilename, EmissionKind, EnumTypes, RetainedTypes,
       GlobalVariables, ImportedEntities, Macros, DWOId, SplitDebugInlining,
       DebugInfoForProfiling, (unsigned)NameTableKind, RangesBaseAddress,
       SysRoot, SDK))
  /// Get or create a distinct or temporary DICompileUnit with the given
  /// operands.
  ///
  /// Provides getDistinct and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param SourceLanguage Source language identity.
  /// \param File Source file metadata.
  /// \param Producer Compiler producer identification string.
  /// \param IsOptimized Whether the compile unit was built optimized.
  /// \param Flags Flags bitfield.
  /// \param RuntimeVersion The runtime version.
  /// \param SplitDebugFilename Split DWARF object filename.
  /// \param EmissionKind Debug info emission kind.
  /// \param EnumTypes The enum types.
  /// \param RetainedTypes Retained types list.
  /// \param GlobalVariables The global variables.
  /// \param ImportedEntities The imported entities.
  /// \param Macros Macros list.
  /// \param DWOId DWO identifier.
  /// \param SplitDebugInlining Whether split DWARF inlining is enabled.
  /// \param DebugInfoForProfiling Whether extra profiling debug info is enabled.
  /// \param NameTableKind Debug name table kind.
  /// \param RangesBaseAddress Whether range lists use a base address.
  /// \param SysRoot SDK or sysroot path string.
  /// \param SDK SDK name string.
  /// \return The metadata node (distinct or temporary as requested).
  DEFINE_MDNODE_GET_DISTINCT_TEMPORARY(
      DICompileUnit,
      (DISourceLanguageName SourceLanguage, Metadata *File, MDString *Producer,
       bool IsOptimized, MDString *Flags, unsigned RuntimeVersion,
       MDString *SplitDebugFilename, unsigned EmissionKind, Metadata *EnumTypes,
       Metadata *RetainedTypes, Metadata *GlobalVariables,
       Metadata *ImportedEntities, Metadata *Macros, uint64_t DWOId,
       bool SplitDebugInlining, bool DebugInfoForProfiling,
       unsigned NameTableKind, bool RangesBaseAddress, MDString *SysRoot,
       MDString *SDK),
      (SourceLanguage, File, Producer, IsOptimized, Flags, RuntimeVersion,
       SplitDebugFilename, EmissionKind, EnumTypes, RetainedTypes,
       GlobalVariables, ImportedEntities, Macros, DWOId, SplitDebugInlining,
       DebugInfoForProfiling, NameTableKind, RangesBaseAddress, SysRoot, SDK))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDICompileUnit clone() const { return cloneImpl(); }

  /// Return the source language.
  /// \return The source language.
  DISourceLanguageName getSourceLanguage() const { return SourceLanguage; }
  /// Return true if this is optimized.
  /// \return true if this is optimized.
  bool isOptimized() const { return IsOptimized; }
  /// Return true if this is debug info for profiling.
  /// \return true if this is debug info for profiling.
  bool isDebugInfoForProfiling() const { return DebugInfoForProfiling; }
  /// Return the runtime version.
  /// \return The runtime version.
  unsigned getRuntimeVersion() const { return RuntimeVersion; }
  /// Return the emission kind.
  /// \return The emission kind.
  DebugEmissionKind getEmissionKind() const {
    return (DebugEmissionKind)EmissionKind;
  }
  // Return true if this CU was compiled with debug info disabled
  /// Return true if this is no debug.
  /// \return true if this is no debug.
  bool isNoDebug() const { return EmissionKind == NoDebug; }
  /// Return true if this is debug directives only.
  /// \return true if this is debug directives only.
  bool isDebugDirectivesOnly() const {
    return EmissionKind == DebugDirectivesOnly;
  }
  /// Return the debug info for profiling.
  /// \return The debug info for profiling.
  bool getDebugInfoForProfiling() const { return DebugInfoForProfiling; }
  /// Return the name table kind.
  /// \return The name table kind.
  DebugNameTableKind getNameTableKind() const {
    return (DebugNameTableKind)NameTableKind;
  }
  /// Return the ranges base address.
  /// \return The ranges base address.
  bool getRangesBaseAddress() const { return RangesBaseAddress; }
  /// Return the producer.
  /// \return The producer.
  StringRef getProducer() const { return getStringOperand(1); }
  /// Return the flags.
  /// \return The flags.
  StringRef getFlags() const { return getStringOperand(2); }
  /// Return the split debug filename.
  /// \return The split debug filename.
  StringRef getSplitDebugFilename() const { return getStringOperand(3); }
  /// Return the enum types.
  /// \return The enum types.
  DICompositeTypeArray getEnumTypes() const {
    return cast_or_null<MDTuple>(getRawEnumTypes());
  }
  /// Return the retained types.
  /// \return The retained types.
  DIScopeArray getRetainedTypes() const {
    return cast_or_null<MDTuple>(getRawRetainedTypes());
  }
  /// Return the global variables.
  /// \return The global variables.
  DIGlobalVariableExpressionArray getGlobalVariables() const {
    return cast_or_null<MDTuple>(getRawGlobalVariables());
  }
  /// Return the imported entities.
  /// \return The imported entities.
  DIImportedEntityArray getImportedEntities() const {
    return cast_or_null<MDTuple>(getRawImportedEntities());
  }
  /// Return the macros.
  /// \return The macros.
  DIMacroNodeArray getMacros() const {
    return cast_or_null<MDTuple>(getRawMacros());
  }
  /// Return the dwo id.
  /// \return The dwo id.
  uint64_t getDWOId() const { return DWOId; }
  /// Set the dwo id.
  /// \param DwoId The dwo id.
  void setDWOId(uint64_t DwoId) { DWOId = DwoId; }
  /// Return the split debug inlining.
  /// \return The split debug inlining.
  bool getSplitDebugInlining() const { return SplitDebugInlining; }
  /// Set the split debug inlining.
  /// \param SplitDebugInlining Whether split DWARF inlining is enabled.
  void setSplitDebugInlining(bool SplitDebugInlining) {
    this->SplitDebugInlining = SplitDebugInlining;
  }
  /// Return the sys root.
  /// \return The sys root.
  StringRef getSysRoot() const { return getStringOperand(9); }
  /// Return the sdk.
  /// \return The sdk.
  StringRef getSDK() const { return getStringOperand(10); }
  /// Target-specific language dialect for DWARF.
  /// \return target-specific language dialect for DWARF.
  uint16_t getDialect() const { return SourceLanguage.getDialect(); }

  /// Return the raw producer operand.
  /// \return The raw producer operand.
  MDString *getRawProducer() const { return getOperandAs<MDString>(1); }
  /// Return the raw flags operand.
  /// \return The raw flags operand.
  MDString *getRawFlags() const { return getOperandAs<MDString>(2); }
  /// Return the raw split debug filename operand.
  /// \return The raw split debug filename operand.
  MDString *getRawSplitDebugFilename() const {
    return getOperandAs<MDString>(3);
  }
  /// Return the raw enum types operand.
  /// \return The raw enum types operand.
  Metadata *getRawEnumTypes() const { return getOperand(4); }
  /// Return the raw retained types operand.
  /// \return The raw retained types operand.
  Metadata *getRawRetainedTypes() const { return getOperand(5); }
  /// Return the raw global variables operand.
  /// \return The raw global variables operand.
  Metadata *getRawGlobalVariables() const { return getOperand(6); }
  /// Return the raw imported entities operand.
  /// \return The raw imported entities operand.
  Metadata *getRawImportedEntities() const { return getOperand(7); }
  /// Return the raw macros operand.
  /// \return The raw macros operand.
  Metadata *getRawMacros() const { return getOperand(8); }
  /// Return the raw sys root operand.
  /// \return The raw sys root operand.
  MDString *getRawSysRoot() const { return getOperandAs<MDString>(9); }
  /// Return the raw sdk operand.
  /// \return The raw sdk operand.
  MDString *getRawSDK() const { return getOperandAs<MDString>(10); }
  /// Replace arrays.
  ///
  /// If this \a isUniqued() and not \a isResolved(), it will be RAUW'ed and
  /// deleted on a uniquing collision.  In practice, uniquing collisions on \a
  /// DICompileUnit should be fairly rare.
  /// @{
  /// \param N Count, index, or node being visited.
  void replaceEnumTypes(DICompositeTypeArray N) {
    replaceOperandWith(4, N.get());
  }
  /// Replace the retained types.
  /// \param N Count, index, or node being visited.
  void replaceRetainedTypes(DITypeArray N) { replaceOperandWith(5, N.get()); }
  /// Replace the global variables.
  /// \param N Count, index, or node being visited.
  void replaceGlobalVariables(DIGlobalVariableExpressionArray N) {
    replaceOperandWith(6, N.get());
  }
  /// Replace the imported entities.
  /// \param N Count, index, or node being visited.
  void replaceImportedEntities(DIImportedEntityArray N) {
    replaceOperandWith(7, N.get());
  }
  /// Replace the macros.
  /// \param N Count, index, or node being visited.
  void replaceMacros(DIMacroNodeArray N) { replaceOperandWith(8, N.get()); }
  /// @}

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DICompileUnitKind;
  }
};

/// A scope for locals.
///
/// A legal scope for lexical blocks, local variables, and debug info
/// locations.  Subclasses are \a DISubprogram, \a DILexicalBlock, and \a
/// DILexicalBlockFile.
class DILocalScope : public DIScope {
protected:
  /// Construct a DILocalScope.
  /// \param C LLVM context that owns the metadata.
  /// \param ID Metadata subclass ID.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Tag DWARF tag for the node.
  /// \param Ops Operand list for the scope.
  DILocalScope(LLVMContext &C, unsigned ID, StorageType Storage, unsigned Tag,
               ArrayRef<Metadata *> Ops)
      : DIScope(C, ID, Storage, Tag, Ops) {}
  /// Destroy this DILocalScope.
  ~DILocalScope() = default;

public:
  /// Get the subprogram for this scope.
  ///
  /// Return this if it's an \a DISubprogram; otherwise, look up the scope
  /// chain.
  /// \return get the subprogram for this scope.
  LLVM_ABI DISubprogram *getSubprogram() const;

  /// Traverses the scope chain rooted at RootScope until it hits a Subprogram,
  /// recreating the chain with "NewSP" instead.
  /// \param RootScope Root local scope to clone.
  /// \param NewSP Subprogram that owns the cloned scope chain.
  /// \param Ctx LLVM context for newly created scope nodes.
  /// \param Cache Map from old scopes to already-cloned scopes.
  /// \return traverses the scope chain rooted at RootScope until it hits a Subprogram,.
  LLVM_ABI static DILocalScope *
  cloneScopeForSubprogram(DILocalScope &RootScope, DISubprogram &NewSP,
                          LLVMContext &Ctx,
                          DenseMap<const MDNode *, MDNode *> &Cache);

  /// Get the first non DILexicalBlockFile scope of this scope.
  ///
  /// Return this if it's not a \a DILexicalBlockFIle; otherwise, look up the
  /// scope chain.
  /// \return get the first non DILexicalBlockFile scope of this scope.
  LLVM_ABI DILocalScope *getNonLexicalBlockFileScope() const;

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DISubprogramKind ||
           MD->getMetadataID() == DILexicalBlockKind ||
           MD->getMetadataID() == DILexicalBlockFileKind;
  }
};

/// Subprogram description. Uses SubclassData1.
class DISubprogram : public DILocalScope {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Line;
  unsigned ScopeLine;
  unsigned VirtualIndex;

  /// In the MS ABI, the implicit 'this' parameter is adjusted in the prologue
  /// of method overrides from secondary bases by this amount. It may be
  /// negative.
  int ThisAdjustment;

public:
  /// Debug info subprogram flags.
  enum DISPFlags : uint32_t {
#define HANDLE_DISP_FLAG(ID, NAME) SPFlag##NAME = ID,
#define DISP_FLAG_LARGEST_NEEDED
#include "llvm/IR/DebugInfoFlags.def"
    /// Non-virtual subprogram (alias of SPFlagZero).
    SPFlagNonvirtual = SPFlagZero,
    /// Mask of virtuality bits (virtual | pure-virtual).
    SPFlagVirtuality = SPFlagVirtual | SPFlagPureVirtual,
    /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
    LLVM_MARK_AS_BITMASK_ENUM(SPFlagLargest)
  };

  /// Return the flag.
  /// \param Flag Flag enumerator or flag name string.
  /// \return The flag.
  LLVM_ABI static DISPFlags getFlag(StringRef Flag);
  /// Return the flag string.
  /// \param Flag Flag enumerator or flag name string.
  /// \return The flag string.
  LLVM_ABI static StringRef getFlagString(DISPFlags Flag);

  /// Split up a flags bitfield for easier printing.
  ///
  /// Split \c Flags into \c SplitFlags, a vector of its components.  Returns
  /// any remaining (unrecognized) bits.
  /// \param Flags Flags bitfield.
  /// \param SplitFlags Receives individual flag components.
  /// \return Any remaining (unrecognized) bits.
  LLVM_ABI static DISPFlags splitFlags(DISPFlags Flags,
                                       SmallVectorImpl<DISPFlags> &SplitFlags);

  // Helper for converting old bitfields to new flags word.
  /// Pack subprogram flags into a DISPFlags bitfield.
  /// \param IsLocalToUnit Whether the symbol is local to its compile unit.
  /// \param IsDefinition Whether this is a definition.
  /// \param IsOptimized Whether the compile unit was built optimized.
  /// \param Virtuality DWARF virtuality code.
  /// \param IsMainSubprogram The is main subprogram.
  /// \return The packed DISPFlags bitfield.
  LLVM_ABI static DISPFlags toSPFlags(bool IsLocalToUnit, bool IsDefinition,
                                      bool IsOptimized,
                                      unsigned Virtuality = SPFlagNonvirtual,
                                      bool IsMainSubprogram = false);

private:
  DIFlags Flags;
  DISPFlags SPFlags;

  /// DI Subprogram.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Line Source line number.
  /// \param ScopeLine The scope line.
  /// \param VirtualIndex Vtable index.
  /// \param ThisAdjustment This-adjustment for a thunk.
  /// \param Flags Flags bitfield.
  /// \param SPFlags Subprogram DISPFlags bitfield.
  /// \param UsesKeyInstructions The uses key instructions.
  /// \param Ops Operand list, or expression opcodes.
  DISubprogram(LLVMContext &C, StorageType Storage, unsigned Line,
               unsigned ScopeLine, unsigned VirtualIndex, int ThisAdjustment,
               DIFlags Flags, DISPFlags SPFlags, bool UsesKeyInstructions,
               ArrayRef<Metadata *> Ops);
  /// Destroy this DISubprogram.
  ~DISubprogram() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param LinkageName Mangled linkage name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param ScopeLine The scope line.
  /// \param ContainingType Containing type for this subprogram.
  /// \param VirtualIndex Vtable index.
  /// \param ThisAdjustment This-adjustment for a thunk.
  /// \param Flags Flags bitfield.
  /// \param SPFlags Subprogram DISPFlags bitfield.
  /// \param Unit Compile unit owning the subprogram.
  /// \param TemplateParams Template parameter list.
  /// \param Declaration Subprogram declaration this definition refers to.
  /// \param RetainedNodes Nodes retained for this subprogram.
  /// \param ThrownTypes Exception types this subprogram may throw.
  /// \param Annotations Annotation metadata tuple.
  /// \param TargetFuncName Target-specific function name.
  /// \param UsesKeyInstructions The uses key instructions.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DISubprogram *
  getImpl(LLVMContext &Context, DIScope *Scope, StringRef Name,
          StringRef LinkageName, DIFile *File, unsigned Line,
          DISubroutineType *Type, unsigned ScopeLine, DIType *ContainingType,
          unsigned VirtualIndex, int ThisAdjustment, DIFlags Flags,
          DISPFlags SPFlags, DICompileUnit *Unit,
          DITemplateParameterArray TemplateParams, DISubprogram *Declaration,
          MDNodeArray RetainedNodes, DITypeArray ThrownTypes,
          DINodeArray Annotations, StringRef TargetFuncName,
          bool UsesKeyInstructions, StorageType Storage,
          bool ShouldCreate = true) {
    return getImpl(Context, Scope, getCanonicalMDString(Context, Name),
                   getCanonicalMDString(Context, LinkageName), File, Line, Type,
                   ScopeLine, ContainingType, VirtualIndex, ThisAdjustment,
                   Flags, SPFlags, Unit, TemplateParams.get(), Declaration,
                   RetainedNodes.get(), ThrownTypes.get(), Annotations.get(),
                   getCanonicalMDString(Context, TargetFuncName),
                   UsesKeyInstructions, Storage, ShouldCreate);
  }

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param LinkageName Mangled linkage name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param ScopeLine The scope line.
  /// \param ContainingType Containing type for this subprogram.
  /// \param VirtualIndex Vtable index.
  /// \param ThisAdjustment This-adjustment for a thunk.
  /// \param Flags Flags bitfield.
  /// \param SPFlags Subprogram DISPFlags bitfield.
  /// \param Unit Compile unit owning the subprogram.
  /// \param TemplateParams Template parameter list.
  /// \param Declaration Subprogram declaration this definition refers to.
  /// \param RetainedNodes Nodes retained for this subprogram.
  /// \param ThrownTypes Exception types this subprogram may throw.
  /// \param Annotations Annotation metadata tuple.
  /// \param TargetFuncName Target-specific function name.
  /// \param UsesKeyInstructions The uses key instructions.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DISubprogram *
  getImpl(LLVMContext &Context, Metadata *Scope, MDString *Name,
          MDString *LinkageName, Metadata *File, unsigned Line, Metadata *Type,
          unsigned ScopeLine, Metadata *ContainingType, unsigned VirtualIndex,
          int ThisAdjustment, DIFlags Flags, DISPFlags SPFlags, Metadata *Unit,
          Metadata *TemplateParams, Metadata *Declaration,
          Metadata *RetainedNodes, Metadata *ThrownTypes, Metadata *Annotations,
          MDString *TargetFuncName, bool UsesKeyInstructions,
          StorageType Storage, bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDISubprogram cloneImpl() const {
    return getTemporary(getContext(), getScope(), getName(), getLinkageName(),
                        getFile(), getLine(), getType(), getScopeLine(),
                        getContainingType(), getVirtualIndex(),
                        getThisAdjustment(), getFlags(), getSPFlags(),
                        getUnit(), getTemplateParams(), getDeclaration(),
                        getRetainedNodes(), getThrownTypes(), getAnnotations(),
                        getTargetFuncName(), getKeyInstructionsEnabled());
  }

public:
  /// Get or create a DISubprogram with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param LinkageName Mangled linkage name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param ScopeLine The scope line.
  /// \param ContainingType Containing type for this subprogram.
  /// \param VirtualIndex Vtable index.
  /// \param ThisAdjustment This-adjustment for a thunk.
  /// \param Flags Flags bitfield.
  /// \param SPFlags Subprogram DISPFlags bitfield.
  /// \param Unit Compile unit owning the subprogram.
  /// \param TemplateParams Template parameter list.
  /// \param Declaration Subprogram declaration this definition refers to.
  /// \param RetainedNodes Nodes retained for this subprogram.
  /// \param ThrownTypes Exception types this subprogram may throw.
  /// \param Annotations Annotation metadata tuple.
  /// \param TargetFuncName Target-specific function name.
  /// \param UsesKeyInstructions The uses key instructions.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(
      DISubprogram,
      (DIScope * Scope, StringRef Name, StringRef LinkageName, DIFile *File,
       unsigned Line, DISubroutineType *Type, unsigned ScopeLine,
       DIType *ContainingType, unsigned VirtualIndex, int ThisAdjustment,
       DIFlags Flags, DISPFlags SPFlags, DICompileUnit *Unit,
       DITemplateParameterArray TemplateParams = nullptr,
       DISubprogram *Declaration = nullptr, MDNodeArray RetainedNodes = nullptr,
       DITypeArray ThrownTypes = nullptr, DINodeArray Annotations = nullptr,
       StringRef TargetFuncName = "", bool UsesKeyInstructions = false),
      (Scope, Name, LinkageName, File, Line, Type, ScopeLine, ContainingType,
       VirtualIndex, ThisAdjustment, Flags, SPFlags, Unit, TemplateParams,
       Declaration, RetainedNodes, ThrownTypes, Annotations, TargetFuncName,
       UsesKeyInstructions))

  /// Get or create a DISubprogram with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param LinkageName Mangled linkage name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param ScopeLine The scope line.
  /// \param ContainingType Containing type for this subprogram.
  /// \param VirtualIndex Vtable index.
  /// \param ThisAdjustment This-adjustment for a thunk.
  /// \param Flags Flags bitfield.
  /// \param SPFlags Subprogram DISPFlags bitfield.
  /// \param Unit Compile unit owning the subprogram.
  /// \param TemplateParams Template parameter list.
  /// \param Declaration Subprogram declaration this definition refers to.
  /// \param RetainedNodes Nodes retained for this subprogram.
  /// \param ThrownTypes Exception types this subprogram may throw.
  /// \param Annotations Annotation metadata tuple.
  /// \param TargetFuncName Target-specific function name.
  /// \param UsesKeyInstructions The uses key instructions.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(
      DISubprogram,
      (Metadata * Scope, MDString *Name, MDString *LinkageName, Metadata *File,
       unsigned Line, Metadata *Type, unsigned ScopeLine,
       Metadata *ContainingType, unsigned VirtualIndex, int ThisAdjustment,
       DIFlags Flags, DISPFlags SPFlags, Metadata *Unit,
       Metadata *TemplateParams = nullptr, Metadata *Declaration = nullptr,
       Metadata *RetainedNodes = nullptr, Metadata *ThrownTypes = nullptr,
       Metadata *Annotations = nullptr, MDString *TargetFuncName = nullptr,
       bool UsesKeyInstructions = false),
      (Scope, Name, LinkageName, File, Line, Type, ScopeLine, ContainingType,
       VirtualIndex, ThisAdjustment, Flags, SPFlags, Unit, TemplateParams,
       Declaration, RetainedNodes, ThrownTypes, Annotations, TargetFuncName,
       UsesKeyInstructions))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDISubprogram clone() const { return cloneImpl(); }

  /// Returns a new temporary DISubprogram with updated Flags
  /// \param NewFlags Replacement DIFlags value.
  /// \return A new temporary DISubprogram with updated Flags.
  TempDISubprogram cloneWithFlags(DIFlags NewFlags) const {
    auto NewSP = clone();
    NewSP->Flags = NewFlags;
    return NewSP;
  }

  /// Return the key instructions enabled.
  /// \return The key instructions enabled.
  bool getKeyInstructionsEnabled() const { return SubclassData1; }

public:
  /// Return the line.
  /// \return The line.
  unsigned getLine() const { return Line; }
  /// Return the virtuality.
  /// \return The virtuality.
  unsigned getVirtuality() const { return getSPFlags() & SPFlagVirtuality; }
  /// Return the virtual index.
  /// \return The virtual index.
  unsigned getVirtualIndex() const { return VirtualIndex; }
  /// Return the this adjustment.
  /// \return The this adjustment.
  int getThisAdjustment() const { return ThisAdjustment; }
  /// Return the scope line.
  /// \return The scope line.
  unsigned getScopeLine() const { return ScopeLine; }
  /// Set the scope line.
  /// \param L The l.
  void setScopeLine(unsigned L) {
    assert(isDistinct());
    ScopeLine = L;
  }
  /// Return the flags.
  /// \return The flags.
  DIFlags getFlags() const { return Flags; }
  /// Return the sp flags.
  /// \return The sp flags.
  DISPFlags getSPFlags() const { return SPFlags; }
  /// Return true if this is local to unit.
  /// \return true if this is local to unit.
  bool isLocalToUnit() const { return getSPFlags() & SPFlagLocalToUnit; }
  /// Return true if this is definition.
  /// \return true if this is definition.
  bool isDefinition() const { return getSPFlags() & SPFlagDefinition; }
  /// Return true if this is optimized.
  /// \return true if this is optimized.
  bool isOptimized() const { return getSPFlags() & SPFlagOptimized; }
  /// Return true if this is main subprogram.
  /// \return true if this is main subprogram.
  bool isMainSubprogram() const { return getSPFlags() & SPFlagMainSubprogram; }

  /// Return true if this is artificial.
  /// \return true if this is artificial.
  bool isArtificial() const { return getFlags() & FlagArtificial; }
  /// Return true if this is private.
  /// \return true if this is private.
  bool isPrivate() const {
    return (getFlags() & FlagAccessibility) == FlagPrivate;
  }
  /// Return true if this is protected.
  /// \return true if this is protected.
  bool isProtected() const {
    return (getFlags() & FlagAccessibility) == FlagProtected;
  }
  /// Return true if this is public.
  /// \return true if this is public.
  bool isPublic() const {
    return (getFlags() & FlagAccessibility) == FlagPublic;
  }
  /// Return true if this is explicit.
  /// \return true if this is explicit.
  bool isExplicit() const { return getFlags() & FlagExplicit; }
  /// Return true if this is prototyped.
  /// \return true if this is prototyped.
  bool isPrototyped() const { return getFlags() & FlagPrototyped; }
  /// Return true if this is name simplified.
  /// \return true if this is name simplified.
  bool isNameSimplified() const { return getFlags() & FlagNameIsSimplified; }
  /// Return true if all calls described.
  /// \return true if all calls described.
  bool areAllCallsDescribed() const {
    return getFlags() & FlagAllCallsDescribed;
  }
  /// Return true if this is pure.
  /// \return true if this is pure.
  bool isPure() const { return getSPFlags() & SPFlagPure; }
  /// Return true if this is elemental.
  /// \return true if this is elemental.
  bool isElemental() const { return getSPFlags() & SPFlagElemental; }
  /// Return true if this is recursive.
  /// \return true if this is recursive.
  bool isRecursive() const { return getSPFlags() & SPFlagRecursive; }
  /// Return true if this is obj c direct.
  /// \return true if this is obj c direct.
  bool isObjCDirect() const { return getSPFlags() & SPFlagObjCDirect; }

  /// Check if this is deleted member function.
  ///
  /// Return true if this subprogram is a C++11 special
  /// member function declared deleted.
  /// \return check if this is deleted member function.
  bool isDeleted() const { return getSPFlags() & SPFlagDeleted; }

  /// Check if this is reference-qualified.
  ///
  /// Return true if this subprogram is a C++11 reference-qualified non-static
  /// member function (void foo() &).
  /// \return check if this is reference-qualified.
  bool isLValueReference() const { return getFlags() & FlagLValueReference; }

  /// Check if this is rvalue-reference-qualified.
  ///
  /// Return true if this subprogram is a C++11 rvalue-reference-qualified
  /// non-static member function (void foo() &&).
  /// \return check if this is rvalue-reference-qualified.
  bool isRValueReference() const { return getFlags() & FlagRValueReference; }

  /// Check if this is marked as noreturn.
  ///
  /// Return true if this subprogram is C++11 noreturn or C11 _Noreturn
  /// \return check if this is marked as noreturn.
  bool isNoReturn() const { return getFlags() & FlagNoReturn; }

  // Check if this routine is a compiler-generated thunk.
  //
  // Returns true if this subprogram is a thunk generated by the compiler.
  /// Return true if this is thunk.
  /// \return true if this is thunk.
  bool isThunk() const { return getFlags() & FlagThunk; }

  /// Return the scope.
  /// \return The scope.
  DIScope *getScope() const { return cast_or_null<DIScope>(getRawScope()); }

  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(2); }
  /// Return the linkage name.
  /// \return The linkage name.
  StringRef getLinkageName() const { return getStringOperand(3); }
  /// Only used by clients of CloneFunction, and only right after the cloning.
  /// \param LN New linkage name string metadata.
  void replaceLinkageName(MDString *LN) { replaceOperandWith(3, LN); }

  /// Return the type.
  /// \return The type.
  DISubroutineType *getType() const {
    return cast_or_null<DISubroutineType>(getRawType());
  }
  /// Return the containing type.
  /// \return The containing type.
  DIType *getContainingType() const {
    return cast_or_null<DIType>(getRawContainingType());
  }
  /// Replace the type.
  /// \param Ty The ty.
  void replaceType(DISubroutineType *Ty) {
    assert(isDistinct() && "Only distinct nodes can mutate");
    /// Replace the operand with.
    /// \param Ty The ty.
    replaceOperandWith(4, Ty);
  }

  /// Return the unit.
  /// \return The unit.
  DICompileUnit *getUnit() const {
    return cast_or_null<DICompileUnit>(getRawUnit());
  }
  /// Replace the unit.
  /// \param CU The cu.
  void replaceUnit(DICompileUnit *CU) { replaceOperandWith(5, CU); }
  /// Return the template params.
  /// \return The template params.
  DITemplateParameterArray getTemplateParams() const {
    return cast_or_null<MDTuple>(getRawTemplateParams());
  }
  /// Return the declaration.
  /// \return The declaration.
  DISubprogram *getDeclaration() const {
    return cast_or_null<DISubprogram>(getRawDeclaration());
  }
  /// Replace the declaration.
  /// \param Decl The decl.
  void replaceDeclaration(DISubprogram *Decl) { replaceOperandWith(6, Decl); }
  /// Return the retained nodes.
  /// \return The retained nodes.
  MDNodeArray getRetainedNodes() const {
    return cast_or_null<MDTuple>(getRawRetainedNodes());
  }
  /// Return the thrown types.
  /// \return The thrown types.
  DITypeArray getThrownTypes() const {
    return cast_or_null<MDTuple>(getRawThrownTypes());
  }
  /// Return the annotations.
  /// \return The annotations.
  DINodeArray getAnnotations() const {
    return cast_or_null<MDTuple>(getRawAnnotations());
  }
  /// Return the target func name.
  /// \return The target func name.
  StringRef getTargetFuncName() const {
    return (getRawTargetFuncName()) ? getStringOperand(12) : StringRef();
  }

  /// Return the raw scope operand.
  /// \return The raw scope operand.
  Metadata *getRawScope() const { return getOperand(1); }
  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(2); }
  /// Return the raw linkage name operand.
  /// \return The raw linkage name operand.
  MDString *getRawLinkageName() const { return getOperandAs<MDString>(3); }
  /// Return the raw type operand.
  /// \return The raw type operand.
  Metadata *getRawType() const { return getOperand(4); }
  /// Return the raw unit operand.
  /// \return The raw unit operand.
  Metadata *getRawUnit() const { return getOperand(5); }
  /// Return the raw declaration operand.
  /// \return The raw declaration operand.
  Metadata *getRawDeclaration() const { return getOperand(6); }
  /// Return the raw retained nodes operand.
  /// \return The raw retained nodes operand.
  Metadata *getRawRetainedNodes() const { return getOperand(7); }
  /// Return the raw containing type operand.
  /// \return The raw containing type operand.
  Metadata *getRawContainingType() const {
    return getNumOperands() > 8 ? getOperandAs<Metadata>(8) : nullptr;
  }
  /// Return the raw template params operand.
  /// \return The raw template params operand.
  Metadata *getRawTemplateParams() const {
    return getNumOperands() > 9 ? getOperandAs<Metadata>(9) : nullptr;
  }
  /// Return the raw thrown types operand.
  /// \return The raw thrown types operand.
  Metadata *getRawThrownTypes() const {
    return getNumOperands() > 10 ? getOperandAs<Metadata>(10) : nullptr;
  }
  /// Return the raw annotations operand.
  /// \return The raw annotations operand.
  Metadata *getRawAnnotations() const {
    return getNumOperands() > 11 ? getOperandAs<Metadata>(11) : nullptr;
  }
  /// Return the raw target func name operand.
  /// \return The raw target func name operand.
  MDString *getRawTargetFuncName() const {
    return getNumOperands() > 12 ? getOperandAs<MDString>(12) : nullptr;
  }

  /// Replace the raw linkage name.
  /// \param LinkageName Mangled linkage name.
  void replaceRawLinkageName(MDString *LinkageName) {
    /// Replace the operand with.
    /// \param LinkageName Mangled linkage name.
    replaceOperandWith(3, LinkageName);
  }
  /// Replace the retained nodes.
  /// \param N Count, index, or node being visited.
  void replaceRetainedNodes(MDNodeArray N) { replaceOperandWith(7, N.get()); }

  /// Retain the given metadata nodes on this subprogram.
  /// \param NodesBegin The nodes begin.
  /// \param NodesEnd The nodes end.
  template <typename IterT> void retainNodes(IterT NodesBegin, IterT NodesEnd) {
    auto RetainedNodes = getRetainedNodes();
    /// append.
    /// \param NodesBegin The nodes begin.
    /// \param NodesEnd The nodes end.
    /// \return append.
    SmallVector<Metadata *> MDs(RetainedNodes.begin(), RetainedNodes.end());
    MDs.append(NodesBegin, NodesEnd);
    replaceRetainedNodes(MDNode::get(getContext(), MDs));
  }

  /// Visit one retained node with the appropriate callback.
  /// \param N Retained metadata node to dispatch.
  /// \param FuncLV Callback invoked for each retained local variable.
  /// \param FuncLabel Callback invoked for each retained label.
  /// \param FuncIE Callback invoked for each retained imported entity.
  /// \param FuncType Callback invoked for each retained type.
  /// \param FuncGVE Callback invoked for each retained global variable
  ///   expression.
  /// \param FuncUnknown Callback invoked for unrecognized retained nodes.
  /// \return The value returned by the selected callback.
  template <typename T, typename MetadataT, typename FuncLVT,
            typename FuncLabelT, typename FuncImportedEntityT,
            typename FuncTypeT, typename FuncGVET, typename FuncUnknownT>
  static T visitRetainedNode(MetadataT *N, FuncLVT &&FuncLV,
                             FuncLabelT &&FuncLabel,
                             FuncImportedEntityT &&FuncIE, FuncTypeT &&FuncType,
                             FuncGVET &&FuncGVE, FuncUnknownT &&FuncUnknown) {
    /// static assert.
    static_assert(std::is_base_of_v<Metadata, MetadataT>,
                  "N must point to Metadata or const Metadata");

    if (auto *LV = dyn_cast<DILocalVariable>(N))
      return FuncLV(LV);
    if (auto *L = dyn_cast<DILabel>(N))
      return FuncLabel(L);
    if (auto *IE = dyn_cast<DIImportedEntity>(N))
      return FuncIE(IE);
    if (auto *Ty = dyn_cast<DIType>(N))
      return FuncType(Ty);
    if (auto *GVE = dyn_cast<DIGlobalVariableExpression>(N))
      return FuncGVE(GVE);
    return FuncUnknown(N);
  }

  /// Returns the scope of subprogram's retainedNodes.
  /// \param N Count, index, or node being visited.
  /// \return The scope of subprogram's retainedNodes.
  LLVM_ABI static const DILocalScope *getRetainedNodeScope(const MDNode *N);
  /// Return the retained node scope.
  /// \param N Count, index, or node being visited.
  /// \return The retained node scope.
  LLVM_ABI static DILocalScope *getRetainedNodeScope(MDNode *N);
  // For use in Verifier.
  /// Return the raw retained node scope operand.
  /// \param N Count, index, or node being visited.
  /// \return The raw retained node scope operand.
  LLVM_ABI static const DIScope *getRawRetainedNodeScope(const MDNode *N);
  /// Return the raw retained node scope operand.
  /// \param N Count, index, or node being visited.
  /// \return The raw retained node scope operand.
  LLVM_ABI static DIScope *getRawRetainedNodeScope(MDNode *N);

  /// Invoke callbacks for every retained node on this subprogram.
  /// \param FuncLV Callback invoked for each retained local variable.
  /// \param FuncLabel Callback invoked for each retained label.
  /// \param FuncIE Callback invoked for each retained imported entity.
  /// \param FuncType Callback invoked for each retained type.
  /// \param FuncGVE Callback invoked for each retained global variable
  ///   expression.
  template <typename FuncLVT, typename FuncLabelT, typename FuncImportedEntityT,
            typename FuncTypeT, typename FuncGVET>
  void forEachRetainedNode(FuncLVT &&FuncLV, FuncLabelT &&FuncLabel,
                           FuncImportedEntityT &&FuncIE, FuncTypeT &&FuncType,
                           FuncGVET &&FuncGVE) {
    for (MDNode *N : getRetainedNodes())
      visitRetainedNode<void>(
          N, FuncLV, FuncLabel, FuncIE, FuncType, FuncGVE,
          [](auto *N) { llvm_unreachable("Unexpected retained node!"); });
  }

  /// Remove cross-subprogram retained-node references after merging.
  ///
  /// Remove cross-subprogram retained-node references after merging.
  ///
  /// When IR modules are merged, typically during LTO, the merged module
  /// may contain several types having the same linkageName. They are
  /// supposed to represent the same type included by multiple source code
  /// files from a single header file.
  ///
  /// DebugTypeODRUniquing feature uniques (deduplicates) such types
  /// based on their linkageName during metadata loading, to speed up
  /// compilation and reduce debug info size.
  ///
  /// However, since function-local types are tracked in DISubprogram's
  /// retainedNodes field, a single local type may be referenced by multiple
  /// DISubprograms via retainedNodes as the result of DebugTypeODRUniquing.
  /// But retainedNodes field of a DISubprogram is meant to hold only
  /// subprogram's own local entities, therefore such references may
  /// cause crashes.
  ///
  /// To address this problem, this method is called for each new subprogram
  /// after module loading. It removes references to types belonging
  /// to other DISubprograms from a subprogram's retainedNodes list.
  /// If a corresponding IR function refers to local scopes from another
  /// subprogram, emitted debug info (e.g. DWARF) should rely
  /// on cross-subprogram references (and cross-CU references, as subprograms
  /// may belong to different compile units). This is also a drawback:
  /// when a subprogram refers to types that are local to another subprogram,
  /// it is more complicated for debugger to properly discover local types
  /// of a current scope for expression evaluation.
  LLVM_ABI void cleanupRetainedNodes();

  /// Filter retained nodes with predicate \p Pred.
  /// \param Pred Predicate selecting retained nodes to keep.
  template <typename T> void cleanupRetainedNodesIf(T &&Pred) {
    MDTuple *RetainedNodes = dyn_cast_or_null<MDTuple>(getRawRetainedNodes());
    // As this is expected to be called during module loading, before
    // stripping old or incorrect debug info, perform minimal sanity check.
    if (!RetainedNodes)
      return;
    // replaceRetainedNodes() should not re-unique DISubprogram if new list is
    // the same pointer.
    replaceRetainedNodes(RetainedNodes->filter(Pred));
  }

  /// Calls SP->cleanupRetainedNodes() for each distinct subprogram in the range.
  /// \param NewDistinctSPs Distinct subprograms whose retained nodes are cleaned.
  template <typename RangeT>
  static void cleanupRetainedNodes(const RangeT &NewDistinctSPs) {
    for (DISubprogram *SP : NewDistinctSPs)
      SP->cleanupRetainedNodes();
  }

  /// Check if this subprogram describes the given function.
  ///
  /// FIXME: Should this be looking through bitcasts?
  /// \param F Function that may be described by this subprogram.
  /// \return check if this subprogram describes the given function.
  LLVM_ABI bool describes(const Function *F) const;

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DISubprogramKind;
  }
};

/// Debug location.
///
/// A debug location in source code, used for debug info and otherwise.
///
/// Uses the SubclassData1, SubclassData16 and SubclassData32
/// Metadata slots.

class DILocation : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;
  uint64_t AtomGroup : 61;
  uint64_t AtomRank : 3;

  /// DI Location.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param AtomGroup The atom group.
  /// \param AtomRank The atom rank.
  /// \param MDs The m ds.
  /// \param ImplicitCode The implicit code.
  DILocation(LLVMContext &C, StorageType Storage, unsigned Line,
             unsigned Column, uint64_t AtomGroup, uint8_t AtomRank,
             ArrayRef<Metadata *> MDs, bool ImplicitCode);
  /// Destroy this DILocation.
  ~DILocation() { dropAllReferences(); }

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param Scope Parent lexical or type scope.
  /// \param InlinedAt Inlined-at location, or null if not inlined.
  /// \param ImplicitCode The implicit code.
  /// \param AtomGroup The atom group.
  /// \param AtomRank The atom rank.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DILocation *
  getImpl(LLVMContext &Context, unsigned Line, unsigned Column, Metadata *Scope,
          Metadata *InlinedAt, bool ImplicitCode, uint64_t AtomGroup,
          uint8_t AtomRank, StorageType Storage, bool ShouldCreate = true);
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param Scope Parent lexical or type scope.
  /// \param InlinedAt Inlined-at location, or null if not inlined.
  /// \param ImplicitCode The implicit code.
  /// \param AtomGroup The atom group.
  /// \param AtomRank The atom rank.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DILocation *getImpl(LLVMContext &Context, unsigned Line,
                             unsigned Column, DILocalScope *Scope,
                             DILocation *InlinedAt, bool ImplicitCode,
                             uint64_t AtomGroup, uint8_t AtomRank,
                             StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Line, Column, static_cast<Metadata *>(Scope),
                   static_cast<Metadata *>(InlinedAt), ImplicitCode, AtomGroup,
                   AtomRank, Storage, ShouldCreate);
  }

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDILocation cloneImpl() const {
    // Get the raw scope/inlinedAt since it is possible to invoke this on
    // a DILocation containing temporary metadata.
    return getTemporary(getContext(), getLine(), getColumn(), getRawScope(),
                        getRawInlinedAt(), isImplicitCode(), getAtomGroup(),
                        getAtomRank());
  }

public:
  /// Return the atom group.
  /// \return The atom group.
  uint64_t getAtomGroup() const { return AtomGroup; }
  /// Return the atom rank.
  /// \return The atom rank.
  uint8_t getAtomRank() const { return AtomRank; }

  /// Return the without atom.
  /// \return The without atom.
  const DILocation *getWithoutAtom() const {
    if (!getAtomGroup() && !getAtomRank())
      return this;
    return get(getContext(), getLine(), getColumn(), getScope(), getInlinedAt(),
               isImplicitCode());
  }

  // Disallow replacing operands.
  /// Replace the operand with.
  /// \param I Operand or argument index.
  /// \param New Replacement metadata value.
  void replaceOperandWith(unsigned I, Metadata *New) = delete;

  /// Get or create a DILocation with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param Scope Parent lexical or type scope.
  /// \param InlinedAt Inlined-at location, or null if not inlined.
  /// \param ImplicitCode Whether this location refers to implicit code.
  /// \param AtomGroup Source atom group identifier.
  /// \param AtomRank Source atom rank within the group.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DILocation,
                    (unsigned Line, unsigned Column, Metadata *Scope,
                     Metadata *InlinedAt = nullptr, bool ImplicitCode = false,
                     uint64_t AtomGroup = 0, uint8_t AtomRank = 0),
                    (Line, Column, Scope, InlinedAt, ImplicitCode, AtomGroup,
                     AtomRank))
  /// Get or create a DILocation with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param Scope Parent lexical or type scope.
  /// \param InlinedAt Inlined-at location, or null if not inlined.
  /// \param ImplicitCode Whether this location refers to implicit code.
  /// \param AtomGroup Source atom group identifier.
  /// \param AtomRank Source atom rank within the group.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DILocation,
                    (unsigned Line, unsigned Column, DILocalScope *Scope,
                     DILocation *InlinedAt = nullptr, bool ImplicitCode = false,
                     uint64_t AtomGroup = 0, uint8_t AtomRank = 0),
                    (Line, Column, Scope, InlinedAt, ImplicitCode, AtomGroup,
                     AtomRank))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDILocation clone() const { return cloneImpl(); }

  /// Return the line.
  /// \return The line.
  unsigned getLine() const { return SubclassData32; }
  /// Return the column.
  /// \return The column.
  unsigned getColumn() const { return SubclassData16; }
  /// Return the scope.
  /// \return The scope.
  DILocalScope *getScope() const { return cast<DILocalScope>(getRawScope()); }

  /// Return the linkage name of Subprogram. If the linkage name is empty,
  /// return scope name (the demangled name).
  /// \return The linkage name of Subprogram. If the linkage name is empty,.
  StringRef getSubprogramLinkageName() const {
    /// Return the subprogram.
    /// \return The subprogram.
    DISubprogram *SP = getScope()->getSubprogram();
    if (!SP)
      return "";
    auto Name = SP->getLinkageName();
    if (!Name.empty())
      return Name;
    return SP->getName();
  }

  /// Return the inlined at.
  /// \return The inlined at.
  DILocation *getInlinedAt() const {
    return cast_or_null<DILocation>(getRawInlinedAt());
  }

  /// Return true if this location marks compiler-inserted implicit code.
  ///
  /// Check if the location corresponds to an implicit code.
  /// When the ImplicitCode flag is true, it means that the Instruction
  /// with this DILocation has been added by the front-end but it hasn't been
  /// written explicitly by the user (e.g. cleanup stuff in C++ put on a closing
  /// bracket). It's useful for code coverage to not show a counter on "empty"
  /// lines.
  /// \return true if this location marks compiler-inserted implicit code.
  bool isImplicitCode() const { return SubclassData1; }
  /// Set the implicit code.
  /// \param ImplicitCode The implicit code.
  void setImplicitCode(bool ImplicitCode) { SubclassData1 = ImplicitCode; }

  /// Return the file.
  /// \return The file.
  DIFile *getFile() const { return getScope()->getFile(); }
  /// Return the filename.
  /// \return The filename.
  StringRef getFilename() const { return getScope()->getFilename(); }
  /// Return the directory.
  /// \return The directory.
  StringRef getDirectory() const { return getScope()->getDirectory(); }
  /// Return the source.
  /// \return The source.
  std::optional<StringRef> getSource() const { return getScope()->getSource(); }

  /// Walk through \a getInlinedAt() and return the \a DILocation of the
  /// outermost call site in the inlining chain.
  /// \return walk through \a getInlinedAt() and return the \a DILocation of the.
  const DILocation *getInlinedAtLocation() const {
    const DILocation *Current = this;
    while (const DILocation *Next = Current->getInlinedAt())
      Current = Next;
    return Current;
  }

  // Return the \a DILocalScope of the outermost call site in the inlining
  // chain.
  /// Return the inlined at scope.
  /// \return The inlined at scope.
  DILocalScope *getInlinedAtScope() const {
    return getInlinedAtLocation()->getScope();
  }

  /// Get the DWARF discriminator.
  ///
  /// DWARF discriminators distinguish identical file locations between
  /// instructions that are on different basic blocks.
  ///
  /// There are 3 components stored in discriminator, from lower bits:
  ///
  /// Base discriminator: assigned by AddDiscriminators pass to identify IRs
  ///                     that are defined by the same source line, but
  ///                     different basic blocks.
  /// Duplication factor: assigned by optimizations that will scale down
  ///                     the execution frequency of the original IR.
  /// Copy Identifier: assigned by optimizations that clones the IR.
  ///                  Each copy of the IR will be assigned an identifier.
  ///
  /// Encoding:
  ///
  /// The above 3 components are encoded into a 32bit unsigned integer in
  /// order. If the lowest bit is 1, the current component is empty, and the
  /// next component will start in the next bit. Otherwise, the current
  /// component is non-empty, and its content starts in the next bit. The
  /// value of each components is either 5 bit or 12 bit: if the 7th bit
  /// is 0, the bit 2~6 (5 bits) are used to represent the component; if the
  /// 7th bit is 1, the bit 2~6 (5 bits) and 8~14 (7 bits) are combined to
  /// represent the component. Thus, the number of bits used for a component
  /// is either 0 (if it and all the next components are empty); 1 - if it is
  /// empty; 7 - if its value is up to and including 0x1f (lsb and msb are both
  /// 0); or 14, if its value is up to and including 0x1ff. Note that the last
  /// component is also capped at 0x1ff, even in the case when both first
  /// components are 0, and we'd technically have 29 bits available.
  ///
  /// For precise control over the data being encoded in the discriminator,
  /// use encodeDiscriminator/decodeDiscriminator.
  /// \return get the DWARF discriminator.

  inline unsigned getDiscriminator() const;

  // For the regular discriminator, it stands for all empty components if all
  // the lowest 3 bits are non-zero and all higher 29 bits are unused(zero by
  // default). Here we fully leverage the higher 29 bits for pseudo probe use.
  // This is the format:
  // [2:0] - 0x7
  // [31:3] - pseudo probe fields guaranteed to be non-zero as a whole
  // So if the lower 3 bits is non-zero and the others has at least one
  // non-zero bit, it guarantees to be a pseudo probe discriminator
  /// Return true if this is pseudo probe discriminator.
  /// \param Discriminator Encoded discriminator value.
  /// \return true if this is pseudo probe discriminator.
  inline static bool isPseudoProbeDiscriminator(unsigned Discriminator) {
    return ((Discriminator & 0x7) == 0x7) && (Discriminator & 0xFFFFFFF8);
  }

  /// Returns a new DILocation with updated \p Discriminator.
  /// \param Discriminator Encoded discriminator value.
  /// \return A new DILocation with updated \p Discriminator.
  inline const DILocation *cloneWithDiscriminator(unsigned Discriminator) const;

  /// Clone this location with a new base discriminator.
  ///
  /// Returns a new DILocation with updated base discriminator \p BD. Only the
  /// base discriminator is set in the new DILocation, the other encoded values
  /// are elided.
  /// If the discriminator cannot be encoded, the function returns std::nullopt.
  ///
  /// \param BD Base discriminator.
  /// \return A temporary clone of this node.
  inline std::optional<const DILocation *>
  cloneWithBaseDiscriminator(unsigned BD) const;

  /// Returns the duplication factor stored in the discriminator, or 1 if no
  /// duplication factor (or 0) is encoded.
  /// \return The duplication factor stored in the discriminator, or 1 if no.
  inline unsigned getDuplicationFactor() const;

  /// Returns the copy identifier stored in the discriminator.
  /// \return The copy identifier stored in the discriminator.
  inline unsigned getCopyIdentifier() const;

  /// Returns the base discriminator stored in the discriminator.
  /// \return The base discriminator stored in the discriminator.
  inline unsigned getBaseDiscriminator() const;

  /// Clone this location with a scaled duplication factor.
  ///
  /// Returns a new DILocation with duplication factor \p DF * current
  /// duplication factor encoded in the discriminator. The current duplication
  /// factor is as defined by getDuplicationFactor().
  /// Returns std::nullopt if encoding failed.
  ///
  /// \param DF Duplication factor.
  /// \return A temporary clone of this node.
  inline std::optional<const DILocation *>
  cloneByMultiplyingDuplicationFactor(unsigned DF) const;

  /// Merge two DILocations into a single location.
  ///
  /// Attempts to merge \p LocA and \p LocB into a single location; see
  /// DebugLoc::getMergedLocation for more details.
  /// NB: When merging the locations of instructions, prefer to use
  /// DebugLoc::getMergedLocation(), as an instruction's DebugLoc may contain
  /// additional metadata that will not be preserved when merging the unwrapped
  /// DILocations.
  ///
  /// \param LocA First location to merge.
  /// \param LocB Second location to merge.
  /// \return merge two DILocations into a single location.
  LLVM_ABI static DILocation *getMergedLocation(DILocation *LocA,
                                                DILocation *LocB);

  /// Merge a list of DILocations into a single location.
  ///
  /// Try to combine the vector of locations passed as input in a single one.
  /// This function applies getMergedLocation() repeatedly left-to-right.
  /// NB: When merging the locations of instructions, prefer to use
  /// DebugLoc::getMergedLocations(), as an instruction's DebugLoc may contain
  /// additional metadata that will not be preserved when merging the unwrapped
  /// DILocations.
  ///
  /// \p Locs: The locations to be merged.
  ///
  /// \param Locs Locations to merge.
  /// \return merge a list of DILocations into a single location.
  LLVM_ABI static DILocation *getMergedLocations(ArrayRef<DILocation *> Locs);

  /// Return the masked discriminator value for an input discrimnator value D
  /// (i.e. zero out the (B+1)-th and above bits for D (B is 0-base).
  /// \return The masked discriminator value for an input discrimnator value D.
  // Example: an input of (0x1FF, 7) returns 0xFF.
  /// Return the masked discriminator.
  /// \param D Discriminator encoding or DenseMap key.
  /// \param B Right-hand operand.
  /// \return The masked discriminator.
  static unsigned getMaskedDiscriminator(unsigned D, unsigned B) {
    return (D & getN1Bits(B));
  }

  /// Return the bits used for base discriminators.
  /// \return The bits used for base discriminators.
  static unsigned getBaseDiscriminatorBits() { return getBaseFSBitEnd(); }

  /// Returns the base discriminator for a given encoded discriminator \p D.
  /// \param D Discriminator encoding or DenseMap key.
  /// \param IsFSDiscriminator Whether to decode as a flow-sensitive
  ///   discriminator.
  /// \return The base discriminator for a given encoded discriminator \p D.
  static unsigned
  getBaseDiscriminatorFromDiscriminator(unsigned D,
                                        bool IsFSDiscriminator = false) {
    // Extract the dwarf base discriminator if it's encoded in the pseudo probe
    // discriminator.
    if (isPseudoProbeDiscriminator(D)) {
      auto DwarfBaseDiscriminator =
          PseudoProbeDwarfDiscriminator::extractDwarfBaseDiscriminator(D);
      if (DwarfBaseDiscriminator)
        return *DwarfBaseDiscriminator;
      // Return the probe id instead of zero for a pseudo probe discriminator.
      // This should help differenciate callsites with same line numbers to
      // achieve a decent AutoFDO profile under -fpseudo-probe-for-profiling,
      // where the original callsite dwarf discriminator is overwritten by
      // callsite probe information.
      return PseudoProbeDwarfDiscriminator::extractProbeIndex(D);
    }

    if (IsFSDiscriminator)
      return getMaskedDiscriminator(D, getBaseDiscriminatorBits());
    return getUnsignedFromPrefixEncoding(D);
  }

  /// Encode base discriminator, duplication factor, and copy identifier.
  ///
  /// Raw encoding of the discriminator. APIs such as cloneWithDuplicationFactor
  /// have certain special case behavior (e.g. treating empty duplication factor
  /// as the value '1').
  /// This API, in conjunction with cloneWithDiscriminator, may be used to
  /// encode the raw values provided.
  ///
  /// \p BD: base discriminator
  /// \p DF: duplication factor
  /// \p CI: copy index
  ///
  /// The return is std::nullopt if the values cannot be encoded in 32 bits -
  /// for example, values for BD or DF larger than 12 bits. Otherwise, the
  /// return is the encoded value.
  ///
  /// \param BD Base discriminator.
  /// \param DF Duplication factor.
  /// \param CI Constant int to fold, or copy identifier.
  /// \return The encoded discriminator, or std::nullopt if the values do not fit.
  LLVM_ABI static std::optional<unsigned>
  encodeDiscriminator(unsigned BD, unsigned DF, unsigned CI);

  /// Raw decoder for values in an encoded discriminator D.
  /// \param D Discriminator encoding or DenseMap key.
  /// \param BD Base discriminator.
  /// \param DF Duplication factor.
  /// \param CI Constant int to fold, or copy identifier.
  LLVM_ABI static void decodeDiscriminator(unsigned D, unsigned &BD,
                                           unsigned &DF, unsigned &CI);

  /// Returns the duplication factor for a given encoded discriminator \p D, or
  /// 1 if no value or 0 is encoded.
  /// \param D Discriminator encoding or DenseMap key.
  /// \return The duplication factor for \p D, or 1 if none is encoded.
  static unsigned getDuplicationFactorFromDiscriminator(unsigned D) {
    if (EnableFSDiscriminator)
      return 1;
    D = getNextComponentInDiscriminator(D);
    unsigned Ret = getUnsignedFromPrefixEncoding(D);
    if (Ret == 0)
      return 1;
    return Ret;
  }

  /// Returns the copy identifier for a given encoded discriminator \p D.
  /// \param D Discriminator encoding or DenseMap key.
  /// \return The copy identifier for a given encoded discriminator \p D.
  static unsigned getCopyIdentifierFromDiscriminator(unsigned D) {
    return getUnsignedFromPrefixEncoding(
        getNextComponentInDiscriminator(getNextComponentInDiscriminator(D)));
  }

  /// Return the raw scope operand.
  /// \return The raw scope operand.
  Metadata *getRawScope() const { return getOperand(0); }
  /// Return the raw inlined at operand.
  /// \return The raw inlined at operand.
  Metadata *getRawInlinedAt() const {
    if (getNumOperands() == 2)
      return getOperand(1);
    return nullptr;
  }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILocationKind;
  }
};

/// Base class for lexical-block scopes.
class DILexicalBlockBase : public DILocalScope {
protected:
  /// Construct a DILexicalBlockBase.
  /// \param C LLVM context that owns the metadata.
  /// \param ID Metadata subclass kind identifier.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Ops Operand list, or expression opcodes.
  LLVM_ABI DILexicalBlockBase(LLVMContext &C, unsigned ID, StorageType Storage,
                              ArrayRef<Metadata *> Ops);
  /// Destroy this DILexicalBlockBase.
  ~DILexicalBlockBase() = default;

public:
  /// Return the scope.
  /// \return The scope.
  DILocalScope *getScope() const { return cast<DILocalScope>(getRawScope()); }

  /// Return the raw scope operand.
  /// \return The raw scope operand.
  Metadata *getRawScope() const { return getOperand(1); }

  /// Replace the scope.
  /// \param Scope Parent lexical or type scope.
  void replaceScope(DIScope *Scope) {
    assert(!isUniqued());
    /// Set the operand.
    /// \param Scope Parent lexical or type scope.
    setOperand(1, Scope);
  }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILexicalBlockKind ||
           MD->getMetadataID() == DILexicalBlockFileKind;
  }
};

/// Debug lexical block.
///
/// Uses the SubclassData32 Metadata slot.
class DILexicalBlock : public DILexicalBlockBase {
  friend class LLVMContextImpl;
  friend class MDNode;

  uint16_t Column;

  DILexicalBlock(LLVMContext &C, StorageType Storage, unsigned Line,
                 unsigned Column, ArrayRef<Metadata *> Ops)
      : DILexicalBlockBase(C, DILexicalBlockKind, Storage, Ops),
        Column(Column) {
    /// assert.
    SubclassData32 = Line;
    assert(Column < (1u << 16) && "Expected 16-bit column");
  }
  /// Destroy this DILexicalBlock.
  ~DILexicalBlock() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DILexicalBlock *getImpl(LLVMContext &Context, DILocalScope *Scope,
                                 DIFile *File, unsigned Line, unsigned Column,
                                 StorageType Storage,
                                 bool ShouldCreate = true) {
    return getImpl(Context, static_cast<Metadata *>(Scope),
                   static_cast<Metadata *>(File), Line, Column, Storage,
                   ShouldCreate);
  }

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DILexicalBlock *getImpl(LLVMContext &Context, Metadata *Scope,
                                          Metadata *File, unsigned Line,
                                          unsigned Column, StorageType Storage,
                                          bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDILexicalBlock cloneImpl() const {
    return getTemporary(getContext(), getScope(), getFile(), getLine(),
                        getColumn());
  }

public:
  /// Get or create a DILexicalBlock with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DILexicalBlock,
                    (DILocalScope * Scope, DIFile *File, unsigned Line,
                     unsigned Column),
                    (Scope, File, Line, Column))
  /// Get or create a DILexicalBlock with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DILexicalBlock,
                    (Metadata * Scope, Metadata *File, unsigned Line,
                     unsigned Column),
                    (Scope, File, Line, Column))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDILexicalBlock clone() const { return cloneImpl(); }

  /// Return the line.
  /// \return The line.
  unsigned getLine() const { return SubclassData32; }
  /// Return the column.
  /// \return The column.
  unsigned getColumn() const { return Column; }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILexicalBlockKind;
  }
};

/// Lexical block that also encodes a DWARF discriminator or file change.
class DILexicalBlockFile : public DILexicalBlockBase {
  friend class LLVMContextImpl;
  friend class MDNode;

  DILexicalBlockFile(LLVMContext &C, StorageType Storage,
                     unsigned Discriminator, ArrayRef<Metadata *> Ops)
      : DILexicalBlockBase(C, DILexicalBlockFileKind, Storage, Ops) {
    SubclassData32 = Discriminator;
  }
  /// Destroy this DILexicalBlockFile.
  ~DILexicalBlockFile() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param File Source file metadata.
  /// \param Discriminator Encoded discriminator value.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DILexicalBlockFile *getImpl(LLVMContext &Context, DILocalScope *Scope,
                                     DIFile *File, unsigned Discriminator,
                                     StorageType Storage,
                                     bool ShouldCreate = true) {
    return getImpl(Context, static_cast<Metadata *>(Scope),
                   static_cast<Metadata *>(File), Discriminator, Storage,
                   ShouldCreate);
  }

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param File Source file metadata.
  /// \param Discriminator Encoded discriminator value.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DILexicalBlockFile *getImpl(LLVMContext &Context,
                                              Metadata *Scope, Metadata *File,
                                              unsigned Discriminator,
                                              StorageType Storage,
                                              bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDILexicalBlockFile cloneImpl() const {
    return getTemporary(getContext(), getScope(), getFile(),
                        getDiscriminator());
  }

public:
  /// Get or create a DILexicalBlockFile with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param File Source file metadata.
  /// \param Discriminator Encoded discriminator value.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DILexicalBlockFile,
                    (DILocalScope * Scope, DIFile *File,
                     unsigned Discriminator),
                    (Scope, File, Discriminator))
  /// Get or create a DILexicalBlockFile with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param File Source file metadata.
  /// \param Discriminator Encoded discriminator value.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DILexicalBlockFile,
                    (Metadata * Scope, Metadata *File, unsigned Discriminator),
                    (Scope, File, Discriminator))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDILexicalBlockFile clone() const { return cloneImpl(); }
  /// Return the discriminator.
  /// \return The discriminator.
  unsigned getDiscriminator() const { return SubclassData32; }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILexicalBlockFileKind;
  }
};

/// Return the discriminator.
/// \return The discriminator.
unsigned DILocation::getDiscriminator() const {
  if (auto *F = dyn_cast<DILexicalBlockFile>(getScope()))
    return F->getDiscriminator();
  return 0;
}

const DILocation *
DILocation::cloneWithDiscriminator(unsigned Discriminator) const {
  /// Return the scope.
  /// \return The scope.
  DIScope *Scope = getScope();
  // Skip all parent DILexicalBlockFile that already have a discriminator
  // assigned. We do not want to have nested DILexicalBlockFiles that have
  // multiple discriminators because only the leaf DILexicalBlockFile's
  // dominator will be used.
  for (auto *LBF = dyn_cast<DILexicalBlockFile>(Scope);
       LBF && LBF->getDiscriminator() != 0;
       LBF = dyn_cast<DILexicalBlockFile>(Scope))
    /// Return the scope.
    Scope = LBF->getScope();
  DILexicalBlockFile *NewScope =
      DILexicalBlockFile::get(getContext(), Scope, getFile(), Discriminator);
  return DILocation::get(getContext(), getLine(), getColumn(), NewScope,
                         getInlinedAt(), isImplicitCode(), getAtomGroup(),
                         getAtomRank());
}

/// Return the base discriminator.
/// \return The base discriminator.
unsigned DILocation::getBaseDiscriminator() const {
  return getBaseDiscriminatorFromDiscriminator(getDiscriminator(),
                                               EnableFSDiscriminator);
}

/// Return the duplication factor.
/// \return The duplication factor.
unsigned DILocation::getDuplicationFactor() const {
  return getDuplicationFactorFromDiscriminator(getDiscriminator());
}

/// Return the copy identifier.
/// \return The copy identifier.
unsigned DILocation::getCopyIdentifier() const {
  return getCopyIdentifierFromDiscriminator(getDiscriminator());
}

std::optional<const DILocation *>
DILocation::cloneWithBaseDiscriminator(unsigned D) const {
  // Do not interfere with pseudo probes. Pseudo probe at a callsite uses
  // the dwarf discriminator to store pseudo probe related information,
  // such as the probe id.
  if (isPseudoProbeDiscriminator(getDiscriminator()))
    return this;

  unsigned BD, DF, CI;

  if (EnableFSDiscriminator) {
    BD = getBaseDiscriminator();
    if (D == BD)
      return this;
    return cloneWithDiscriminator(D);
  }

  decodeDiscriminator(getDiscriminator(), BD, DF, CI);
  if (D == BD)
    return this;
  if (std::optional<unsigned> Encoded = encodeDiscriminator(D, DF, CI))
    return cloneWithDiscriminator(*Encoded);
  return std::nullopt;
}

std::optional<const DILocation *>
DILocation::cloneByMultiplyingDuplicationFactor(unsigned DF) const {
  assert(!EnableFSDiscriminator && "FSDiscriminator should not call this.");
  // Do no interfere with pseudo probes. Pseudo probe doesn't need duplication
  // factor support as samples collected on cloned probes will be aggregated.
  // Also pseudo probe at a callsite uses the dwarf discriminator to store
  // pseudo probe related information, such as the probe id.
  if (isPseudoProbeDiscriminator(getDiscriminator()))
    return this;

  /// Return the duplication factor.
  /// \return The duplication factor.
  DF *= getDuplicationFactor();
  if (DF <= 1)
    return this;

  unsigned BD = getBaseDiscriminator();
  unsigned CI = getCopyIdentifier();
  if (std::optional<unsigned> D = encodeDiscriminator(BD, DF, CI))
    return cloneWithDiscriminator(*D);
  return std::nullopt;
}

/// Debug namespace scope.
///
/// Uses the SubclassData1 Metadata slot.
class DINamespace : public DIScope {
  friend class LLVMContextImpl;
  friend class MDNode;

  /// DI Namespace.
  /// \param Context LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ExportSymbols Whether the namespace exports its symbols.
  /// \param Ops Operand list, or expression opcodes.
  DINamespace(LLVMContext &Context, StorageType Storage, bool ExportSymbols,
              ArrayRef<Metadata *> Ops);
  /// Destroy this DINamespace.
  ~DINamespace() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param ExportSymbols Whether the namespace exports its symbols.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DINamespace *getImpl(LLVMContext &Context, DIScope *Scope,
                              StringRef Name, bool ExportSymbols,
                              StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Scope, getCanonicalMDString(Context, Name),
                   ExportSymbols, Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param ExportSymbols Whether the namespace exports its symbols.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DINamespace *getImpl(LLVMContext &Context, Metadata *Scope,
                                       MDString *Name, bool ExportSymbols,
                                       StorageType Storage,
                                       bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDINamespace cloneImpl() const {
    return getTemporary(getContext(), getScope(), getName(),
                        getExportSymbols());
  }

public:
  /// Get or create a DINamespace with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param ExportSymbols Whether the namespace exports its symbols.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DINamespace,
                    (DIScope * Scope, StringRef Name, bool ExportSymbols),
                    (Scope, Name, ExportSymbols))
  /// Get or create a DINamespace with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param ExportSymbols Whether the namespace exports its symbols.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DINamespace,
                    (Metadata * Scope, MDString *Name, bool ExportSymbols),
                    (Scope, Name, ExportSymbols))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDINamespace clone() const { return cloneImpl(); }

  /// Return the export symbols.
  /// \return The export symbols.
  bool getExportSymbols() const { return SubclassData1; }
  /// Return the scope.
  /// \return The scope.
  DIScope *getScope() const { return cast_or_null<DIScope>(getRawScope()); }
  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(2); }

  /// Return the raw scope operand.
  /// \return The raw scope operand.
  Metadata *getRawScope() const { return getOperand(1); }
  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(2); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DINamespaceKind;
  }
};

/// Represents a module in the programming language, for example, a Clang
/// module, or a Fortran module.
///
/// Uses the SubclassData1 and SubclassData32 Metadata slots.
class DIModule : public DIScope {
  friend class LLVMContextImpl;
  friend class MDNode;

  /// DI Module.
  /// \param Context LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param LineNo Source line number.
  /// \param IsDecl The is decl.
  /// \param Ops Operand list, or expression opcodes.
  DIModule(LLVMContext &Context, StorageType Storage, unsigned LineNo,
           bool IsDecl, ArrayRef<Metadata *> Ops);
  /// Destroy this DIModule.
  ~DIModule() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param File Source file metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param ConfigurationMacros The configuration macros.
  /// \param IncludePath The include path.
  /// \param APINotesFile The api notes file.
  /// \param LineNo Source line number.
  /// \param IsDecl The is decl.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIModule *getImpl(LLVMContext &Context, DIFile *File, DIScope *Scope,
                           StringRef Name, StringRef ConfigurationMacros,
                           StringRef IncludePath, StringRef APINotesFile,
                           unsigned LineNo, bool IsDecl, StorageType Storage,
                           bool ShouldCreate = true) {
    return getImpl(Context, File, Scope, getCanonicalMDString(Context, Name),
                   getCanonicalMDString(Context, ConfigurationMacros),
                   getCanonicalMDString(Context, IncludePath),
                   getCanonicalMDString(Context, APINotesFile), LineNo, IsDecl,
                   Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param File Source file metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param ConfigurationMacros The configuration macros.
  /// \param IncludePath The include path.
  /// \param APINotesFile The api notes file.
  /// \param LineNo Source line number.
  /// \param IsDecl The is decl.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIModule *
  getImpl(LLVMContext &Context, Metadata *File, Metadata *Scope, MDString *Name,
          MDString *ConfigurationMacros, MDString *IncludePath,
          MDString *APINotesFile, unsigned LineNo, bool IsDecl,
          StorageType Storage, bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIModule cloneImpl() const {
    return getTemporary(getContext(), getFile(), getScope(), getName(),
                        getConfigurationMacros(), getIncludePath(),
                        getAPINotesFile(), getLineNo(), getIsDecl());
  }

public:
  /// Get or create a DIModule with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param File Source file metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param ConfigurationMacros Configuration macros for the module.
  /// \param IncludePath Include path for the module.
  /// \param APINotesFile API notes file for the module.
  /// \param LineNo Source line number.
  /// \param IsDecl Whether this is a module declaration.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIModule,
                    (DIFile * File, DIScope *Scope, StringRef Name,
                     StringRef ConfigurationMacros, StringRef IncludePath,
                     StringRef APINotesFile, unsigned LineNo,
                     bool IsDecl = false),
                    (File, Scope, Name, ConfigurationMacros, IncludePath,
                     APINotesFile, LineNo, IsDecl))
  /// Get or create a DIModule with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param File Source file metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param ConfigurationMacros Configuration macros for the module.
  /// \param IncludePath Include path for the module.
  /// \param APINotesFile API notes file for the module.
  /// \param LineNo Source line number.
  /// \param IsDecl Whether this is a module declaration.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIModule,
                    (Metadata * File, Metadata *Scope, MDString *Name,
                     MDString *ConfigurationMacros, MDString *IncludePath,
                     MDString *APINotesFile, unsigned LineNo,
                     bool IsDecl = false),
                    (File, Scope, Name, ConfigurationMacros, IncludePath,
                     APINotesFile, LineNo, IsDecl))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIModule clone() const { return cloneImpl(); }

  /// Return the scope.
  /// \return The scope.
  DIScope *getScope() const { return cast_or_null<DIScope>(getRawScope()); }
  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(2); }
  /// Return the configuration macros.
  /// \return The configuration macros.
  StringRef getConfigurationMacros() const { return getStringOperand(3); }
  /// Return the include path.
  /// \return The include path.
  StringRef getIncludePath() const { return getStringOperand(4); }
  /// Return the api notes file.
  /// \return The api notes file.
  StringRef getAPINotesFile() const { return getStringOperand(5); }
  /// Return the line no.
  /// \return The line no.
  unsigned getLineNo() const { return SubclassData32; }
  /// Return the is decl.
  /// \return The is decl.
  bool getIsDecl() const { return SubclassData1; }

  /// Return the raw scope operand.
  /// \return The raw scope operand.
  Metadata *getRawScope() const { return getOperand(1); }
  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(2); }
  /// Return the raw configuration macros operand.
  /// \return The raw configuration macros operand.
  MDString *getRawConfigurationMacros() const {
    return getOperandAs<MDString>(3);
  }
  /// Return the raw include path operand.
  /// \return The raw include path operand.
  MDString *getRawIncludePath() const { return getOperandAs<MDString>(4); }
  /// Return the raw api notes file operand.
  /// \return The raw api notes file operand.
  MDString *getRawAPINotesFile() const { return getOperandAs<MDString>(5); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIModuleKind;
  }
};

/// Base class for template parameters.
///
/// Uses the SubclassData1 Metadata slot.
class DITemplateParameter : public DINode {
protected:
  /// Construct a DITemplateParameter.
  /// \param Context LLVM context that owns the metadata.
  /// \param ID Metadata subclass ID.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Tag DWARF tag for the node.
  /// \param IsDefault Whether this is a default template argument.
  /// \param Ops Operand list for the parameter.
  DITemplateParameter(LLVMContext &Context, unsigned ID, StorageType Storage,
                      unsigned Tag, bool IsDefault, ArrayRef<Metadata *> Ops)
      : DINode(Context, ID, Storage, Tag, Ops) {
    SubclassData1 = IsDefault;
  }
  /// Destroy this DITemplateParameter.
  ~DITemplateParameter() = default;

public:
  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(0); }
  /// Return the type.
  /// \return The type.
  DIType *getType() const { return cast_or_null<DIType>(getRawType()); }

  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(0); }
  /// Return the raw type operand.
  /// \return The raw type operand.
  Metadata *getRawType() const { return getOperand(1); }
  /// Return true if this is default.
  /// \return true if this is default.
  bool isDefault() const { return SubclassData1; }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DITemplateTypeParameterKind ||
           MD->getMetadataID() == DITemplateValueParameterKind;
  }
};

/// Type template parameter.
class DITemplateTypeParameter : public DITemplateParameter {
  friend class LLVMContextImpl;
  friend class MDNode;

  /// DI Template Type Parameter.
  /// \param Context LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param IsDefault Whether this is a default template argument.
  /// \param Ops Operand list, or expression opcodes.
  DITemplateTypeParameter(LLVMContext &Context, StorageType Storage,
                          bool IsDefault, ArrayRef<Metadata *> Ops);
  /// Destroy this DITemplateTypeParameter.
  ~DITemplateTypeParameter() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param Type Type metadata.
  /// \param IsDefault Whether this is a default template argument.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DITemplateTypeParameter *getImpl(LLVMContext &Context, StringRef Name,
                                          DIType *Type, bool IsDefault,
                                          StorageType Storage,
                                          bool ShouldCreate = true) {
    return getImpl(Context, getCanonicalMDString(Context, Name), Type,
                   IsDefault, Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param Type Type metadata.
  /// \param IsDefault Whether this is a default template argument.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DITemplateTypeParameter *
  getImpl(LLVMContext &Context, MDString *Name, Metadata *Type, bool IsDefault,
          StorageType Storage, bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDITemplateTypeParameter cloneImpl() const {
    return getTemporary(getContext(), getName(), getType(), isDefault());
  }

public:
  /// Get or create a DITemplateTypeParameter with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param Type Type metadata.
  /// \param IsDefault Whether this is a default template argument.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DITemplateTypeParameter,
                    (StringRef Name, DIType *Type, bool IsDefault),
                    (Name, Type, IsDefault))
  /// Get or create a DITemplateTypeParameter with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param Type Type metadata.
  /// \param IsDefault Whether this is a default template argument.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DITemplateTypeParameter,
                    (MDString * Name, Metadata *Type, bool IsDefault),
                    (Name, Type, IsDefault))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDITemplateTypeParameter clone() const { return cloneImpl(); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DITemplateTypeParameterKind;
  }
};

/// Non-type (value) template parameter.
class DITemplateValueParameter : public DITemplateParameter {
  friend class LLVMContextImpl;
  friend class MDNode;

  DITemplateValueParameter(LLVMContext &Context, StorageType Storage,
                           unsigned Tag, bool IsDefault,
                           ArrayRef<Metadata *> Ops)
      : DITemplateParameter(Context, DITemplateValueParameterKind, Storage, Tag,
                            IsDefault, Ops) {}
  /// Destroy this DITemplateValueParameter.
  ~DITemplateValueParameter() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param Type Type metadata.
  /// \param IsDefault Whether this is a default template argument.
  /// \param Value Enumerator or template value.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DITemplateValueParameter *getImpl(LLVMContext &Context, unsigned Tag,
                                           StringRef Name, DIType *Type,
                                           bool IsDefault, Metadata *Value,
                                           StorageType Storage,
                                           bool ShouldCreate = true) {
    return getImpl(Context, Tag, getCanonicalMDString(Context, Name), Type,
                   IsDefault, Value, Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param Type Type metadata.
  /// \param IsDefault Whether this is a default template argument.
  /// \param Value Enumerator or template value.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DITemplateValueParameter *
  getImpl(LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *Type,
          bool IsDefault, Metadata *Value, StorageType Storage,
          bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDITemplateValueParameter cloneImpl() const {
    return getTemporary(getContext(), getTag(), getName(), getType(),
                        isDefault(), getValue());
  }

public:
  /// Get or create a DITemplateValueParameter with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param Type Type metadata.
  /// \param IsDefault Whether this is a default template argument.
  /// \param Value Enumerator or template value.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DITemplateValueParameter,
                    (unsigned Tag, StringRef Name, DIType *Type, bool IsDefault,
                     Metadata *Value),
                    (Tag, Name, Type, IsDefault, Value))
  /// Get or create a DITemplateValueParameter with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Name Source-level name.
  /// \param Type Type metadata.
  /// \param IsDefault Whether this is a default template argument.
  /// \param Value Enumerator or template value.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DITemplateValueParameter,
                    (unsigned Tag, MDString *Name, Metadata *Type,
                     bool IsDefault, Metadata *Value),
                    (Tag, Name, Type, IsDefault, Value))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDITemplateValueParameter clone() const { return cloneImpl(); }

  /// Return the value.
  /// \return The value.
  Metadata *getValue() const { return getOperand(2); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DITemplateValueParameterKind;
  }
};

/// Base class for variables.
///
/// Uses the SubclassData32 Metadata slot.
class DIVariable : public DINode {
  unsigned Line;

protected:
  /// Construct a DIVariable.
  /// \param C LLVM context that owns the metadata.
  /// \param ID Metadata subclass kind identifier.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Line Source line number.
  /// \param Ops Operand list, or expression opcodes.
  /// \param AlignInBits ABI alignment in bits.
  LLVM_ABI DIVariable(LLVMContext &C, unsigned ID, StorageType Storage,
                      signed Line, ArrayRef<Metadata *> Ops,
                      uint32_t AlignInBits = 0);
  /// Destroy this DIVariable.
  ~DIVariable() = default;

public:
  /// Return the line.
  /// \return The line.
  unsigned getLine() const { return Line; }
  /// Return the scope.
  /// \return The scope.
  DIScope *getScope() const { return cast_or_null<DIScope>(getRawScope()); }
  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(1); }
  /// Return the file.
  /// \return The file.
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }
  /// Return the type.
  /// \return The type.
  DIType *getType() const { return cast_or_null<DIType>(getRawType()); }
  /// Return the align in bits.
  /// \return The align in bits.
  uint32_t getAlignInBits() const { return SubclassData32; }
  /// Return the align in bytes.
  /// \return The align in bytes.
  uint32_t getAlignInBytes() const { return getAlignInBits() / CHAR_BIT; }
  /// Determines the size of the variable's type.
  /// \return determines the size of the variable's type.
  LLVM_ABI std::optional<uint64_t> getSizeInBits() const;

  /// Return the signedness of this variable's type, or std::nullopt if this
  /// type is neither signed nor unsigned.
  /// \return The signedness of this variable's type, or std::nullopt if this.
  std::optional<DIBasicType::Signedness> getSignedness() const {
    if (auto *BT = dyn_cast<DIBasicType>(getType()))
      return BT->getSignedness();
    return std::nullopt;
  }

  /// Return the filename.
  /// \return The filename.
  StringRef getFilename() const {
    if (auto *F = getFile())
      return F->getFilename();
    return "";
  }

  /// Return the directory.
  /// \return The directory.
  StringRef getDirectory() const {
    if (auto *F = getFile())
      return F->getDirectory();
    return "";
  }

  /// Return the source.
  /// \return The source.
  std::optional<StringRef> getSource() const {
    if (auto *F = getFile())
      return F->getSource();
    return std::nullopt;
  }

  /// Return the raw scope operand.
  /// \return The raw scope operand.
  Metadata *getRawScope() const { return getOperand(0); }
  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(1); }
  /// Return the raw file operand.
  /// \return The raw file operand.
  Metadata *getRawFile() const { return getOperand(2); }
  /// Return the raw type operand.
  /// \return The raw type operand.
  Metadata *getRawType() const { return getOperand(3); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILocalVariableKind ||
           MD->getMetadataID() == DIGlobalVariableKind;
  }
};

/// DWARF expression.
///
/// This is (almost) a DWARF expression that modifies the location of a
/// variable, or the location of a single piece of a variable, or (when using
/// DW_OP_stack_value) is the constant variable value.
///
/// TODO: Co-allocate the expression elements.
/// TODO: Separate from MDNode, or otherwise drop Distinct and Temporary
/// storage types.
class DIExpression : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  std::vector<uint64_t> Elements;

  DIExpression(LLVMContext &C, StorageType Storage, ArrayRef<uint64_t> Elements)
      : MDNode(C, DIExpressionKind, Storage, {}),
        Elements(Elements.begin(), Elements.end()) {}
  /// Destroy this DIExpression.
  ~DIExpression() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Elements Elements array or expression element words.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIExpression *getImpl(LLVMContext &Context,
                                        ArrayRef<uint64_t> Elements,
                                        StorageType Storage,
                                        bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIExpression cloneImpl() const {
    return getTemporary(getContext(), getElements());
  }

public:
  /// Get or create a DIExpression with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Elements Elements array or expression element words.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIExpression, (ArrayRef<uint64_t> Elements), (Elements))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIExpression clone() const { return cloneImpl(); }

  /// Return the elements.
  /// \return The elements.
  ArrayRef<uint64_t> getElements() const { return Elements; }

  /// Return the num elements.
  /// \return The num elements.
  unsigned getNumElements() const { return Elements.size(); }

  /// Return the element.
  /// \param I Operand or argument index.
  /// \return The element.
  uint64_t getElement(unsigned I) const {
    assert(I < Elements.size() && "Index out of range");
    return Elements[I];
  }

  /// Sign information for a constant DIExpression.
  enum SignedOrUnsignedConstant {
    /// Constant is signed.
    SignedConstant,
    /// Constant is unsigned.
    UnsignedConstant
  };
  /// Determine whether this represents a constant value, if so
  /// \return determine whether this represents a constant value, if so.
  // return it's sign information.
  /// Return true if this is constant.
  /// \return true if this is constant.
  LLVM_ABI std::optional<SignedOrUnsignedConstant> isConstant() const;

  /// Return the number of unique DW_OP_LLVM_arg location operands.
  ///
  /// Return the number of unique location operands referred to (via
  /// DW_OP_LLVM_arg) in this expression; this is not necessarily the number of
  /// instances of DW_OP_LLVM_arg within the expression.
  /// For example, for the expression:
  /// (DW_OP_LLVM_arg 0, DW_OP_LLVM_arg 1, DW_OP_plus,
  /// DW_OP_LLVM_arg 0, DW_OP_mul)
  /// This function would return 2, as there are two unique location operands
  /// (0 and 1).
  /// \return The number of unique DW_OP_LLVM_arg location operands.
  LLVM_ABI uint64_t getNumLocationOperands() const;

  /// Iterator over DIExpression element words.
  using element_iterator = ArrayRef<uint64_t>::iterator;

  /// Return the elements begin.
  /// \return The elements begin.
  element_iterator elements_begin() const { return getElements().begin(); }
  /// Return the elements end.
  /// \return The elements end.
  element_iterator elements_end() const { return getElements().end(); }

  /// A lightweight wrapper around an expression operand.
  ///
  /// TODO: Store arguments directly and change \a DIExpression to store a
  /// range of these.
  class ExprOperand {
    const uint64_t *Op = nullptr;

  public:
    /// Construct a ExprOperand.
    ExprOperand() = default;
    /// Construct an operand view over expression words.
    /// \param Op Pointer to the first word of the operand.
    explicit ExprOperand(const uint64_t *Op) : Op(Op) {}

    /// Return true if this operand view is non-null.
    /// \return true if this operand view is non-null.
    explicit operator bool() const { return Op != nullptr; }

    /// Return a pointer to this operand's opcode word.
    /// \return A pointer to this operand's opcode word.
    const uint64_t *get() const { return Op; }

    /// Get the operand code.
    ///
    /// The operand has to be present.
    /// \return get the operand code.
    uint64_t getOp() const {
      assert(Op && "operand is not present");
      return *Op;
    }

    /// Return true if this is \p Opcode.
    /// \param Opcode DWARF expression opcode.
    /// \return true if this is \p Opcode.
    bool is(uint64_t Opcode) const { return getOp() == Opcode; }

    /// Get an argument to the operand.
    ///
    /// Never returns the operand itself. The operand has to be present and \p I
    /// has to be less than getNumArgs().
    /// \param I Operand or argument index.
    /// \return get an argument to the operand.
    uint64_t getArg(unsigned I) const {
      assert(Op && "operand is not present");
      return Op[I + 1];
    }

    /// Return the num args.
    /// \return The num args.
    unsigned getNumArgs() const { return getSize() - 1; }

    /// Return the size of the operand.
    ///
    /// Return the number of elements in the operand (1 + args).
    /// \return The size of the operand.
    LLVM_ABI unsigned getSize() const;

    /// Return true if CodeGen handles this operand without adding bytes to the
    /// DWARF expression.
    /// \return true if CodeGen handles this operand without adding bytes to the.
    LLVM_ABI bool isNonEmitting() const;

    /// Append the elements of this operand to \p V.
    /// \param V Destination vector, or source DebugVariable.
    void appendToVector(SmallVectorImpl<uint64_t> &V) const {
      V.append(get(), get() + getSize());
    }
  };

  // Typed views name an ExprOperand's arguments. Use cast<FragmentOp>(Op) for a
  // known opcode and dyn_cast<ArgOp>(Op) for a conditional match. A failed
  // dyn_cast returns an empty view, which tests false and holds no operand to
  // read, so check it before calling an accessor. Keep using ExprOperand for
  // operations without a typed view.
  //
  // A view takes an operand rather than an optional one. A cursor hands back
  // std::optional<ExprOperand>, so check it and then dereference it.
  // dyn_cast_if_present does not compile on std::optional<ExprOperand>, because
  // an operand is constructible from a null pointer, which leaves
  // ValueIsPresent ambiguous between its optional and its nullable
  // specialization.

  /// A view of a DW_OP_LLVM_arg operation.
  class ArgOp : public ExprOperand {
    template <typename To, typename From, typename Enable>
    friend struct llvm::CastInfo;

    /// Expr Operand.
    /// \param Op Expression operand or view.
    explicit ArgOp(ExprOperand Op) : ExprOperand(Op) {}

  public:
    /// Return the location operand index.
    /// \return The location operand index.
    uint64_t getIndex() const { return getArg(0); }

    /// Check whether \p MD is this kind of metadata.
    /// \param Op Expression operand or view.
    /// \return true if \p MD is this kind of metadata.
    LLVM_ABI static bool classof(const ExprOperand *Op);
  };

  /// A view of a DW_OP_LLVM_fragment operation.
  class FragmentOp : public ExprOperand {
    template <typename To, typename From, typename Enable>
    friend struct llvm::CastInfo;

    /// Expr Operand.
    /// \param Op Expression operand or view.
    explicit FragmentOp(ExprOperand Op) : ExprOperand(Op) {}

  public:
    /// Return the fragment offset in bits.
    /// \return The fragment offset in bits.
    uint64_t getOffsetInBits() const { return getArg(0); }

    /// Return the fragment size in bits.
    /// \return The fragment size in bits.
    uint64_t getSizeInBits() const { return getArg(1); }

    /// Check whether \p MD is this kind of metadata.
    /// \param Op Expression operand or view.
    /// \return true if \p MD is this kind of metadata.
    LLVM_ABI static bool classof(const ExprOperand *Op);
  };

  /// A view of the DW_OP_LLVM_extract_bits_[sz]ext operations.
  class ExtractBitsOp : public ExprOperand {
    template <typename To, typename From, typename Enable>
    friend struct llvm::CastInfo;

    /// Expr Operand.
    /// \param Op Expression operand or view.
    explicit ExtractBitsOp(ExprOperand Op) : ExprOperand(Op) {}

  public:
    /// Return the extract offset in bits.
    /// \return The extract offset in bits.
    uint64_t getOffsetInBits() const { return getArg(0); }

    /// Return the extract size in bits.
    /// \return The extract size in bits.
    uint64_t getSizeInBits() const { return getArg(1); }

    /// Return whether the extracted value is sign-extended.
    /// \return Whether the extracted value is sign-extended.
    LLVM_ABI bool isSigned() const;

    /// Check whether \p MD is this kind of metadata.
    /// \param Op Expression operand or view.
    /// \return true if \p MD is this kind of metadata.
    LLVM_ABI static bool classof(const ExprOperand *Op);
  };

  /// A view of a DW_OP_LLVM_convert operation.
  class ConvertOp : public ExprOperand {
    template <typename To, typename From, typename Enable>
    friend struct llvm::CastInfo;

    /// Expr Operand.
    /// \param Op Expression operand or view.
    explicit ConvertOp(ExprOperand Op) : ExprOperand(Op) {}

  public:
    /// Return the destination size in bits.
    /// \return The destination size in bits.
    uint64_t getBitSize() const { return getArg(0); }

    /// Return the raw destination type encoding.
    /// \return The raw destination type encoding.
    uint64_t getEncoding() const { return getArg(1); }

    /// Check whether \p MD is this kind of metadata.
    /// \param Op Expression operand or view.
    /// \return true if \p MD is this kind of metadata.
    LLVM_ABI static bool classof(const ExprOperand *Op);
  };

  /// A view of a DW_OP_LLVM_entry_value operation.
  class EntryValueOp : public ExprOperand {
    template <typename To, typename From, typename Enable>
    friend struct llvm::CastInfo;

    /// Expr Operand.
    /// \param Op Expression operand or view.
    explicit EntryValueOp(ExprOperand Op) : ExprOperand(Op) {}

  public:
    /// Return how many operations an entry value covers.
    ///
    /// Return the number of operations the entry value covers. The count
    /// includes the operation that precedes it, so the operations that follow
    /// are one fewer than this.
    /// \return How many operations an entry value covers.
    uint64_t getNumOperations() const { return getArg(0); }

    /// Check whether \p MD is this kind of metadata.
    /// \param Op Expression operand or view.
    /// \return true if \p MD is this kind of metadata.
    LLVM_ABI static bool classof(const ExprOperand *Op);
  };

  /// A view of a DW_OP_LLVM_tag_offset operation.
  class TagOffsetOp : public ExprOperand {
    template <typename To, typename From, typename Enable>
    friend struct llvm::CastInfo;

    /// Expr Operand.
    /// \param Op Expression operand or view.
    explicit TagOffsetOp(ExprOperand Op) : ExprOperand(Op) {}

  public:
    /// Return the offset a memory tag is derived from. How a target derives
    /// the tag from it is implementation defined.
    /// \return The offset a memory tag is derived from. How a target derives.
    uint64_t getTagOffset() const { return getArg(0); }

    /// Check whether \p MD is this kind of metadata.
    /// \param Op Expression operand or view.
    /// \return true if \p MD is this kind of metadata.
    LLVM_ABI static bool classof(const ExprOperand *Op);
  };

  /// A view of a DW_OP_constu operation.
  class ConstuOp : public ExprOperand {
    template <typename To, typename From, typename Enable>
    friend struct llvm::CastInfo;

    /// Expr Operand.
    /// \param Op Expression operand or view.
    explicit ConstuOp(ExprOperand Op) : ExprOperand(Op) {}

  public:
    /// Return the unsigned constant value.
    /// \return The unsigned constant value.
    uint64_t getValue() const { return getArg(0); }

    /// Check whether \p MD is this kind of metadata.
    /// \param Op Expression operand or view.
    /// \return true if \p MD is this kind of metadata.
    LLVM_ABI static bool classof(const ExprOperand *Op);
  };

  /// A view of a DW_OP_plus_uconst operation.
  class PlusUconstOp : public ExprOperand {
    template <typename To, typename From, typename Enable>
    friend struct llvm::CastInfo;

    /// Expr Operand.
    /// \param Op Expression operand or view.
    explicit PlusUconstOp(ExprOperand Op) : ExprOperand(Op) {}

  public:
    /// Return the unsigned offset.
    /// \return The unsigned offset.
    uint64_t getOffset() const { return getArg(0); }

    /// Check whether \p MD is this kind of metadata.
    /// \param Op Expression operand or view.
    /// \return true if \p MD is this kind of metadata.
    LLVM_ABI static bool classof(const ExprOperand *Op);
  };

  /// An iterator for expression operands.
  class expr_op_iterator {
    ExprOperand Op;

  public:
    /// Standard iterator category.
    using iterator_category = std::input_iterator_tag;
    /// Standard iterator value type.
    using value_type = ExprOperand;
    /// Standard iterator difference type.
    using difference_type = std::ptrdiff_t;
    /// Standard iterator pointer type.
    using pointer = value_type *;
    /// Standard iterator reference type.
    using reference = value_type &;

    /// Construct a expr_op_iterator.
    expr_op_iterator() = default;
    /// Construct an iterator at the given element position.
    /// \param I Underlying element iterator position.
    explicit expr_op_iterator(element_iterator I) : Op(I) {}

    /// Return the base.
    /// \return The base.
    element_iterator getBase() const { return Op.get(); }
    /// Dereference this iterator.
    /// \return dereference this iterator.
    const ExprOperand &operator*() const { return Op; }
    /// Access the operand through this iterator.
    /// \return access the operand through this iterator.
    const ExprOperand *operator->() const { return &Op; }

    /// Advance this iterator.
    /// \return advance this iterator.
    expr_op_iterator &operator++() {
      increment();
      return *this;
    }
    /// Advance this iterator.
    /// \param Unused Postfix dummy argument.
    /// \return advance this iterator.
    expr_op_iterator operator++(int Unused) {
      expr_op_iterator T(*this);
      increment();
      return T;
    }

    /// Get the next iterator.
    ///
    /// \a std::next() doesn't work because this is technically an
    /// input_iterator, but it's a perfectly valid operation.  This is an
    /// accessor to provide the same functionality.
    /// \return An iterator advanced to the next operand.
    expr_op_iterator getNext() const { return ++expr_op_iterator(*this); }

    /// Return true if the two values compare equal.
    /// \param X Other iterator to compare against.
    /// \return true if the two values compare equal.
    bool operator==(const expr_op_iterator &X) const {
      return getBase() == X.getBase();
    }
    /// Return true if the two values compare unequal.
    /// \param X Other iterator to compare against.
    /// \return true if the two values compare unequal.
    bool operator!=(const expr_op_iterator &X) const {
      return getBase() != X.getBase();
    }

  private:
    /// increment.
    void increment() { Op = ExprOperand(getBase() + Op.getSize()); }
  };

  /// Visit the elements via ExprOperand wrappers.
  ///
  /// These range iterators visit elements through \a ExprOperand wrappers.
  /// This is not guaranteed to be a valid range unless \a isValid() gives \c
  /// true.
  ///
  /// \pre \a isValid() gives \c true.
  /// @{
  /// \return visit the elements via ExprOperand wrappers.
  expr_op_iterator expr_op_begin() const {
    return expr_op_iterator(elements_begin());
  }
  /// Return the expr op end.
  /// \return The expr op end.
  expr_op_iterator expr_op_end() const {
    return expr_op_iterator(elements_end());
  }
  /// Return the expr ops.
  /// \return The expr ops.
  iterator_range<expr_op_iterator> expr_ops() const {
    return {expr_op_begin(), expr_op_end()};
  }
  /// @}

  /// Return true if this is valid.
  /// \return true if this is valid.
  LLVM_ABI bool isValid() const;

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIExpressionKind;
  }

  /// Return whether the first element a DW_OP_deref.
  /// \return Whether the first element a DW_OP_deref.
  LLVM_ABI bool startsWithDeref() const;

  /// Return whether there is exactly one operator and it is a DW_OP_deref;
  /// \return Whether there is exactly one operator and it is a DW_OP_deref;.
  LLVM_ABI bool isDeref() const;

  /// Fragment describing a bit-range within a variable.
  using FragmentInfo = DbgVariableFragmentInfo;

  /// Return how many bits in this expression still carry an active value.
  ///
  /// Return the number of bits that have an active value, i.e. those that
  /// aren't known to be zero/sign (depending on the type of Var) and which
  /// are within the size of this fragment (if it is one). If we can't deduce
  /// anything from the expression this will return the size of Var.
  ///
  /// \param Var Debug variable associated with the expression.
  /// \return How many bits in this expression still carry an active value.
  LLVM_ABI std::optional<uint64_t> getActiveBits(DIVariable *Var);

  /// Retrieve the details of this fragment expression.
  /// \param Start Start iterator over expression operands.
  /// \param End End iterator over expression operands.
  /// \return Fragment offset/size if present; otherwise std::nullopt.
  LLVM_ABI static std::optional<FragmentInfo>
  getFragmentInfo(expr_op_iterator Start, expr_op_iterator End);

  /// Retrieve the details of this fragment expression.
  /// \return Fragment offset/size if present; otherwise std::nullopt.
  std::optional<FragmentInfo> getFragmentInfo() const {
    return getFragmentInfo(expr_op_begin(), expr_op_end());
  }

  /// Return whether this is a piece of an aggregate variable.
  /// \return true if this is a piece of an aggregate variable.
  bool isFragment() const { return getFragmentInfo().has_value(); }

  /// Return whether this is an implicit location description.
  /// \return true if this is an implicit location description.
  LLVM_ABI bool isImplicit() const;

  /// Return whether the location is computed on the expression stack, meaning
  /// it cannot be a simple register location.
  /// \return Whether the location is computed on the expression stack, meaning.
  LLVM_ABI bool isComplex() const;

  /// Return true if this expression uses a single starting location.
  ///
  /// Return whether the evaluated expression makes use of a single location at
  /// the start of the expression, i.e. if it contains only a single
  /// DW_OP_LLVM_arg op as its first operand, or if it contains none.
  /// \return true if this expression uses a single starting location.
  LLVM_ABI bool isSingleLocationExpression() const;

  /// Return expression elements, skipping a leading DW_OP_LLVM_arg 0.
  ///
  /// Returns a reference to the elements contained in this expression, skipping
  /// past the leading `DW_OP_LLVM_arg, 0` if one is present.
  /// Similar to `convertToNonVariadicExpression`, but faster and cheaper - it
  /// does not check whether the expression is a single-location expression, and
  /// it returns elements rather than creating a new DIExpression.
  /// \return Expression elements, skipping a leading DW_OP_LLVM_arg 0, or
  ///   \c std::nullopt if the expression is not available.
  LLVM_ABI std::optional<ArrayRef<uint64_t>>
  getSingleLocationExpressionElements() const;

  /// Convert an expression into one suitable for an undef debug value.
  ///
  /// Convert an expression into one suitable for an undef debug value.
  ///
  /// Removes all elements from \p Expr that do not apply to an undef debug
  /// value, which includes every operator that computes the value/location on
  /// the DWARF stack, including any DW_OP_LLVM_arg elements (making the result
  /// of this function always a single-location expression) while leaving
  /// everything that defines what the computed value applies to, i.e. the
  /// fragment information.
  ///
  /// \param Expr DIExpression to transform or query.
  /// \return convert an expression into one suitable for an undef debug value.
  LLVM_ABI static const DIExpression *
  convertToUndefExpression(const DIExpression *Expr);

  /// Convert a non-variadic expression into variadic form.
  ///
  /// If \p Expr is a non-variadic expression (i.e. one that does not contain
  /// DW_OP_LLVM_arg), returns \p Expr converted to variadic form by adding a
  /// leading [DW_OP_LLVM_arg, 0] to the expression; otherwise returns \p Expr.
  ///
  /// \param Expr DIExpression to transform or query.
  /// \return convert a non-variadic expression into variadic form.
  LLVM_ABI static const DIExpression *
  convertToVariadicExpression(const DIExpression *Expr);

  /// Convert a single-location expression to non-variadic form.
  ///
  /// If \p Expr is a valid single-location expression, i.e. it refers to only a
  /// single debug operand at the start of the expression, then return that
  /// expression in a non-variadic form by removing DW_OP_LLVM_arg from the
  /// expression if it is present; otherwise returns std::nullopt.
  /// See also `getSingleLocationExpressionElements` above, which skips
  /// checking `isSingleLocationExpression` and returns a list of elements
  /// rather than a DIExpression.
  ///
  /// \param Expr DIExpression to transform or query.
  /// \return convert a single-location expression to non-variadic form.
  LLVM_ABI static std::optional<const DIExpression *>
  convertToNonVariadicExpression(const DIExpression *Expr);

  /// Rewrite expression ops into a canonical variadic form.
  ///
  /// Inserts the elements of \p Expr into \p Ops modified to a canonical form,
  /// which uses DW_OP_LLVM_arg (i.e. is a variadic expression) and folds the
  /// implied derefence from the \p IsIndirect flag into the expression. This
  /// allows us to check equivalence between expressions with differing
  /// directness or variadicness.
  ///
  /// \param Ops Operand list, or expression opcodes.
  /// \param Expr DIExpression to transform or query.
  /// \param IsIndirect Whether the location is an indirect value.
  LLVM_ABI static void canonicalizeExpressionOps(SmallVectorImpl<uint64_t> &Ops,
                                                 const DIExpression *Expr,
                                                 bool IsIndirect);

  /// Return true if two debug values produce equivalent DWARF expressions.
  ///
  /// Determines whether two debug values should produce equivalent DWARF
  /// expressions, using their DIExpressions and directness, ignoring the
  /// differences between otherwise identical expressions in variadic and
  /// non-variadic form and not considering the debug operands.
  /// \p FirstExpr is the DIExpression for the first debug value.
  /// \p FirstIndirect should be true if the first debug value is indirect; in
  /// IR this should be true for dbg.declare intrinsics and false for
  /// dbg.values, and in MIR this should be true only for DBG_VALUE instructions
  /// whose second operand is an immediate value.
  /// \p SecondExpr and \p SecondIndirect have the same meaning as the prior
  /// arguments, but apply to the second debug value.
  ///
  /// \param FirstExpr First expression to compare.
  /// \param FirstIndirect Whether the first location is indirect.
  /// \param SecondExpr Second expression to compare.
  /// \param SecondIndirect Whether the second location is indirect.
  /// \return true if two debug values produce equivalent DWARF expressions.
  LLVM_ABI static bool isEqualExpression(const DIExpression *FirstExpr,
                                         bool FirstIndirect,
                                         const DIExpression *SecondExpr,
                                         bool SecondIndirect);

  /// Append \p Ops with operations to apply the \p Offset.
  /// \param Ops Operand list, or expression opcodes.
  /// \param Offset Byte or bit offset.
  LLVM_ABI static void appendOffset(SmallVectorImpl<uint64_t> &Ops,
                                    int64_t Offset);

  /// Extract a leading constant address offset from expression ops.
  /// \param Ops Operand list, or expression opcodes.
  /// \param OffsetInBytes Receives the extracted byte offset.
  /// \param RemainingOps Receives the remaining expression ops.
  /// \return extract a leading constant address offset from expression ops.
  LLVM_ABI static bool
  extractLeadingOffset(ArrayRef<uint64_t> Ops, int64_t &OffsetInBytes,
                       SmallVectorImpl<uint64_t> &RemainingOps);

  /// If this is a constant offset, extract it. If there is no expression,
  /// return true with an offset of zero.
  /// \param Offset Byte or bit offset.
  /// \return if this is a constant offset, extract it. If there is no expression,.
  LLVM_ABI bool extractIfOffset(int64_t &Offset) const;

  /// Extract a leading constant address offset from an expression.
  ///
  /// Assuming that the expression operates on an address, extract a constant
  /// offset and the successive ops. Return false if the expression contains
  /// any incompatible ops (including non-zero DW_OP_LLVM_args - only a single
  /// address operand to the expression is permitted).
  ///
  /// We don't try very hard to interpret the expression because we assume that
  /// foldConstantMath has canonicalized the expression.
  ///
  /// \param OffsetInBytes Receives the extracted byte offset.
  /// \param RemainingOps Receives the remaining expression ops.
  /// \return extract a leading constant address offset from an expression.
  LLVM_ABI bool
  extractLeadingOffset(int64_t &OffsetInBytes,
                       SmallVectorImpl<uint64_t> &RemainingOps) const;

  /// Returns true iff this DIExpression contains at least one instance of
  /// `DW_OP_LLVM_arg, n` for all n in [0, N).
  /// \param N Count, index, or node being visited.
  /// \return true iff this DIExpression contains at least one instance of.
  LLVM_ABI bool hasAllLocationOps(unsigned N) const;

  /// Checks if the last 4 elements of the expression are DW_OP_constu <DWARF
  /// Address Space> DW_OP_swap DW_OP_xderef and extracts the <DWARF Address
  /// Space>.
  /// \param Expr DIExpression to transform or query.
  /// \param AddrClass Receives the extracted DWARF address class.
  /// \return checks if the last 4 elements of the expression are DW_OP_constu <DWARF.
  LLVM_ABI static const DIExpression *
  extractAddressClass(const DIExpression *Expr, unsigned &AddrClass);

  /// Flags controlling DIExpression::prepend.
  enum PrependOps : uint8_t {
    /// Apply a constant offset with no deref.
    ApplyOffset = 0,
    /// Insert DW_OP_deref before the offset.
    DerefBefore = 1 << 0,
    /// Insert DW_OP_deref after the offset.
    DerefAfter = 1 << 1,
    /// Mark the result as a DWARF stack value.
    StackValue = 1 << 2,
    /// Wrap the location as a DWARF entry value.
    EntryValue = 1 << 3
  };

  /// Prepend \p DIExpr with a deref and offset operation and optionally turn it
  /// into a stack value or/and an entry value.
  /// \param Expr DIExpression to transform or query.
  /// \param Flags Flags bitfield.
  /// \param Offset Byte or bit offset.
  /// \return prepend \p DIExpr with a deref and offset operation and optionally turn it.
  LLVM_ABI static DIExpression *prepend(const DIExpression *Expr, uint8_t Flags,
                                        int64_t Offset = 0);

  /// Prepend \p DIExpr with the given opcodes and optionally turn it into a
  /// stack value.
  /// \param Expr DIExpression to transform or query.
  /// \param Ops Operand list, or expression opcodes.
  /// \param StackValue Whether the result should be a DWARF stack value.
  /// \param EntryValue Whether to wrap the location as an entry value.
  /// \return prepend \p DIExpr with the given opcodes and optionally turn it into a.
  LLVM_ABI static DIExpression *prependOpcodes(const DIExpression *Expr,
                                               SmallVectorImpl<uint64_t> &Ops,
                                               bool StackValue = false,
                                               bool EntryValue = false);

  /// Append opcodes to a DIExpression.
  ///
  /// Append the opcodes \p Ops to \p DIExpr. Unlike \ref appendToStack, the
  /// returned expression is a stack value only if \p DIExpr is a stack value.
  /// If \p DIExpr describes a fragment, the returned expression will describe
  /// the same fragment.
  ///
  /// \param Expr DIExpression to transform or query.
  /// \param Ops Operand list, or expression opcodes.
  /// \return append opcodes to a DIExpression.
  LLVM_ABI static DIExpression *append(const DIExpression *Expr,
                                       ArrayRef<uint64_t> Ops);

  /// Append opcodes after ensuring the expression is a stack value.
  ///
  /// Convert \p DIExpr into a stack value if it isn't one already by appending
  /// DW_OP_deref if needed, and appending \p Ops to the resulting expression.
  /// If \p DIExpr describes a fragment, the returned expression will describe
  /// the same fragment.
  ///
  /// \param Expr DIExpression to transform or query.
  /// \param Ops Operand list, or expression opcodes.
  /// \return append opcodes after ensuring the expression is a stack value.
  LLVM_ABI static DIExpression *appendToStack(const DIExpression *Expr,
                                              ArrayRef<uint64_t> Ops);

  /// Append opcodes to each use of a given DW_OP_LLVM_arg.
  ///
  /// Create a copy of \p Expr by appending the given list of \p Ops to each
  /// instance of the operand `DW_OP_LLVM_arg, \p ArgNo`. This is used to
  /// modify a specific location used by \p Expr, such as when salvaging that
  /// location.
  ///
  /// \param Expr DIExpression to transform or query.
  /// \param Ops Operand list, or expression opcodes.
  /// \param ArgNo DW_OP_LLVM_arg index to modify.
  /// \param StackValue Whether the result should be a DWARF stack value.
  /// \return append opcodes to each use of a given DW_OP_LLVM_arg.
  LLVM_ABI static DIExpression *appendOpsToArg(const DIExpression *Expr,
                                               ArrayRef<uint64_t> Ops,
                                               unsigned ArgNo,
                                               bool StackValue = false);

  /// Remap DW_OP_LLVM_arg indices in a DIExpression.
  ///
  /// Create a copy of \p Expr with each instance of
  /// `DW_OP_LLVM_arg, \p OldArg` replaced with `DW_OP_LLVM_arg, \p NewArg`,
  /// and each instance of `DW_OP_LLVM_arg, Arg` with `DW_OP_LLVM_arg, Arg - 1`
  /// for all Arg > \p OldArg.
  /// This is used when replacing one of the operands of a debug value list
  /// with another operand in the same list and deleting the old operand.
  ///
  /// \param Expr DIExpression to transform or query.
  /// \param OldArg Existing DW_OP_LLVM_arg index to replace.
  /// \param NewArg Replacement DW_OP_LLVM_arg index.
  /// \return remap DW_OP_LLVM_arg indices in a DIExpression.
  LLVM_ABI static DIExpression *replaceArg(const DIExpression *Expr,
                                           uint64_t OldArg, uint64_t NewArg);

  /// Create a DIExpression describing one fragment of an aggregate.
  ///
  /// Create a DIExpression to describe one part of an aggregate variable that
  /// is fragmented across multiple Values. The DW_OP_LLVM_fragment operation
  /// will be appended to the elements of \c Expr. If \c Expr already contains
  /// a \c DW_OP_LLVM_fragment \c OffsetInBits is interpreted as an offset
  /// into the existing fragment.
  ///
  /// \param OffsetInBits Offset of the piece in bits.
  /// \param SizeInBits   Size of the piece in bits.
  /// \return             Creating a fragment expression may fail if \c Expr
  /// contains arithmetic operations that would be
  /// truncated.
  ///
  /// \param Expr DIExpression to transform or query.
  LLVM_ABI static std::optional<DIExpression *>
  createFragmentExpression(const DIExpression *Expr, unsigned OffsetInBits,
                           unsigned SizeInBits);

  /// Compare the relative position of two fragments.
  ///
  /// Determine the relative position of the fragments passed in.
  /// Returns -1 if this is entirely before Other, 0 if this and Other overlap,
  /// 1 if this is entirely after Other.
  ///
  /// \param A Left-hand operand.
  /// \param B Right-hand operand.
  /// \return compare the relative position of two fragments.
  static int fragmentCmp(const FragmentInfo &A, const FragmentInfo &B) {
    uint64_t l1 = A.OffsetInBits;
    uint64_t l2 = B.OffsetInBits;
    uint64_t r1 = l1 + A.SizeInBits;
    uint64_t r2 = l2 + B.SizeInBits;
    if (r1 <= l2)
      return -1;
    else if (r2 <= l1)
      return 1;
    else
      return 0;
  }

  /// Computes a fragment, bit-extract operation if needed, and new constant
  /// offset to describe a part of a variable covered by some memory.
  ///
  /// The memory region starts at:
  ///   \p SliceStart + \p SliceOffsetInBits
  /// And is size:
  ///   \p SliceSizeInBits
  ///
  /// The location of the existing variable fragment \p VarFrag is:
  ///   \p DbgPtr + \p DbgPtrOffsetInBits + \p DbgExtractOffsetInBits.
  ///
  /// It is intended that these arguments are derived from a debug record:
  /// - \p DbgPtr is the (single) DIExpression operand.
  /// - \p DbgPtrOffsetInBits is the constant offset applied to \p DbgPtr.
  /// - \p DbgExtractOffsetInBits is the offset from a
  ///   DW_OP_LLVM_bit_extract_[sz]ext operation.
  ///
  /// Results and return value:
  /// - Return false if the result can't be calculated for any reason.
  /// - \p Result is set to nullopt if the intersect equals \p VarFrag.
  /// - \p Result contains a zero-sized fragment if there's no intersect.
  /// - \p OffsetFromLocationInBits is set to the difference between the first
  ///   bit of the variable location and the first bit of the slice. The
  ///   magnitude of a negative value therefore indicates the number of bits
  ///   into the variable fragment that the memory region begins.
  ///
  /// We don't pass in a debug record directly to get the constituent parts
  /// and offsets because different debug records store the information in
  /// different places (dbg_assign has two DIExpressions - one contains the
  /// fragment info for the entire intrinsic).
  /// \param DL Debug location used for validation or fragment math.
  /// \param SliceStart Start of the memory slice being intersected.
  /// \param SliceOffsetInBits Bit offset of the slice within the variable.
  /// \param SliceSizeInBits Bit size of the slice.
  /// \param DbgPtr Debug pointer value for the location.
  /// \param DbgPtrOffsetInBits Bit offset applied to the debug pointer.
  /// \param DbgExtractOffsetInBits Bit offset extracted from the expression.
  /// \param VarFrag Variable fragment being intersected.
  /// \param Result Receives the intersecting fragment, if any.
  /// \param OffsetFromLocationInBits Receives the bit offset from the location.
  /// \return computes a fragment, bit-extract operation if needed, and new constant.
  LLVM_ABI static bool calculateFragmentIntersect(
      const DataLayout &DL, const Value *SliceStart, uint64_t SliceOffsetInBits,
      uint64_t SliceSizeInBits, const Value *DbgPtr, int64_t DbgPtrOffsetInBits,
      int64_t DbgExtractOffsetInBits, DIExpression::FragmentInfo VarFrag,
      std::optional<DIExpression::FragmentInfo> &Result,
      int64_t &OffsetFromLocationInBits);

  /// Opcode sequence used to append a zero or sign extension.
  using ExtOps = std::array<uint64_t, 6>;

  /// Returns the ops for a zero- or sign-extension in a DIExpression.
  /// \param FromSize Source bit width for the extension.
  /// \param ToSize Destination bit width for the extension.
  /// \param Signed True for sign-extend; false for zero-extend.
  /// \return The ops for a zero- or sign-extension in a DIExpression.
  LLVM_ABI static ExtOps getExtOps(unsigned FromSize, unsigned ToSize,
                                   bool Signed);

  /// Append a zero- or sign-extension to \p Expr. Converts the expression to a
  /// stack value if it isn't one already.
  /// \param Expr DIExpression to transform or query.
  /// \param FromSize Source bit width for the extension.
  /// \param ToSize Destination bit width for the extension.
  /// \param Signed True for sign-extend; false for zero-extend.
  /// \return append a zero- or sign-extension to \p Expr. Converts the expression to a.
  LLVM_ABI static DIExpression *appendExt(const DIExpression *Expr,
                                          unsigned FromSize, unsigned ToSize,
                                          bool Signed);

  /// Check if fragments overlap between a pair of FragmentInfos.
  /// \param A Left-hand operand.
  /// \param B Right-hand operand.
  /// \return check if fragments overlap between a pair of FragmentInfos.
  static bool fragmentsOverlap(const FragmentInfo &A, const FragmentInfo &B) {
    return fragmentCmp(A, B) == 0;
  }

  /// Determine the relative position of the fragments described by this
  /// DIExpression and \p Other. Calls static fragmentCmp implementation.
  /// \param Other Other fragment or expression to compare against.
  /// \return determine the relative position of the fragments described by this.
  int fragmentCmp(const DIExpression *Other) const {
    auto Fragment1 = *getFragmentInfo();
    auto Fragment2 = *Other->getFragmentInfo();
    return fragmentCmp(Fragment1, Fragment2);
  }

  /// Check if fragments overlap between this DIExpression and \p Other.
  /// \param Other Other fragment or expression to compare against.
  /// \return check if fragments overlap between this DIExpression and \p Other.
  bool fragmentsOverlap(const DIExpression *Other) const {
    if (!isFragment() || !Other->isFragment())
      return true;
    return fragmentCmp(Other) == 0;
  }

  /// Check if the expression consists of exactly one entry value operand.
  /// (This is the only configuration of entry values that is supported.)
  /// \return check if the expression consists of exactly one entry value operand.
  LLVM_ABI bool isEntryValue() const;

  /// Constant-fold a leading constant operand in a DIExpression.
  ///
  /// Try to shorten an expression with an initial constant operand.
  /// Returns a new expression and constant on success, or the original
  /// expression and constant on failure.
  ///
  /// \param CI Constant int to fold, or copy identifier.
  /// \return constant-fold a leading constant operand in a DIExpression.
  LLVM_ABI std::pair<DIExpression *, const ConstantInt *>
  constantFold(const ConstantInt *CI);

  /// Fold constant math operations in a DIExpression.
  ///
  /// Try to shorten an expression with constant math operations that can be
  /// evaluated at compile time. Returns a new expression on success, or the old
  /// expression if there is nothing to be reduced.
  /// \return fold constant math operations in a DIExpression.
  LLVM_ABI DIExpression *foldConstantMath();
};

/// Cast traits for DIExpression typed operand views.
template <typename To, typename From>
struct CastInfo<
    To, From,
    std::enable_if_t<
        std::is_same_v<std::remove_const_t<From>, DIExpression::ExprOperand> &&
        !std::is_same_v<std::remove_const_t<To>, DIExpression::ExprOperand>>>
    : CastIsPossible<To, From>,
      DefaultDoCastIfPossible<To, From, CastInfo<To, From>> {
  /// Cast an ExprOperand into typed view \c To.
  /// \param Op Source expression operand.
  /// \return cast an ExprOperand into typed view \c To.
  static To doCast(const From &Op) { return To(Op); }
  /// Return an empty typed view when the cast fails.
  /// \return An empty typed view when the cast fails.
  static To castFailed() { return To(DIExpression::ExprOperand()); }
};

/// Treat a default-constructed expression operand as absent.
template <> struct ValueIsPresent<DIExpression::ExprOperand> {
  /// Unwrapped type produced by ValueIsPresent.
  using UnwrappedType = DIExpression::ExprOperand;

  /// Return true if this is present.
  /// \param Op Expression operand or view.
  /// \return true if this is present.
  static bool isPresent(const DIExpression::ExprOperand &Op) {
    return bool(Op);
  }

  /// Unwrap the present value.
  /// \param Op Expression operand or view.
  /// \return The unwrapped expression operand.
  static DIExpression::ExprOperand &unwrapValue(DIExpression::ExprOperand &Op) {
    return Op;
  }
};

/// Return true if the two values compare equal.
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// \return true if the two values compare equal.
inline bool operator==(const DIExpression::FragmentInfo &A,
                       const DIExpression::FragmentInfo &B) {
  return std::tie(A.SizeInBits, A.OffsetInBits) ==
         std::tie(B.SizeInBits, B.OffsetInBits);
}

/// Return true if this value orders before the other.
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// \return true if this value orders before the other.
inline bool operator<(const DIExpression::FragmentInfo &A,
                      const DIExpression::FragmentInfo &B) {
  return std::tie(A.SizeInBits, A.OffsetInBits) <
         std::tie(B.SizeInBits, B.OffsetInBits);
}

/// DenseMapInfo for DIExpression fragment descriptors.
template <> struct DenseMapInfo<DIExpression::FragmentInfo> {
  /// Alias for a variable fragment descriptor.
  using FragInfo = DIExpression::FragmentInfo;
  /// Maximum \c uint64_t value used as a sentinel in fragment keys.
  static const uint64_t MaxVal = std::numeric_limits<uint64_t>::max();

  /// Return the hash value.
  /// \param Frag Fragment key to hash or compare.
  /// \return The hash value.
  static unsigned getHashValue(const FragInfo &Frag) {
    return (Frag.SizeInBits & 0xffff) << 16 | (Frag.OffsetInBits & 0xffff);
  }

  /// Return true if this is equal.
  /// \param A Left-hand operand.
  /// \param B Right-hand operand.
  /// \return true if this is equal.
  static bool isEqual(const FragInfo &A, const FragInfo &B) { return A == B; }
};

/// Holds a DIExpression and keeps track of how many operands have been consumed
/// so far.
class DIExpressionCursor {
  DIExpression::expr_op_iterator Start, End;

public:
  /// Construct a DIExpressionCursor.
  /// \param Expr DIExpression to transform or query.
  DIExpressionCursor(const DIExpression *Expr) {
    if (!Expr) {
      assert(Start == End);
      return;
    }
    /// Return the expr op begin.
    Start = Expr->expr_op_begin();
    /// Return the expr op end.
    End = Expr->expr_op_end();
  }

  /// Construct a DIExpressionCursor over raw expression elements.
  /// \param Expr Expression element words to iterate.
  DIExpressionCursor(ArrayRef<uint64_t> Expr)
      : Start(Expr.begin()), End(Expr.end()) {}

  /// Construct a DIExpressionCursor.
  /// \param Other Cursor to copy.
  DIExpressionCursor(const DIExpressionCursor &Other) = default;

  /// Consume one operation.
  /// \return consume one operation.
  std::optional<DIExpression::ExprOperand> take() {
    if (Start == End)
      return std::nullopt;
    return *(Start++);
  }

  /// Consume N operations.
  /// \param N Count, index, or node being visited.
  void consume(unsigned N) { std::advance(Start, N); }

  /// Return the current operation.
  /// \return The current operation.
  std::optional<DIExpression::ExprOperand> peek() const {
    if (Start == End)
      return std::nullopt;
    return *(Start);
  }

  /// Return the next operation.
  /// \return The next operation.
  std::optional<DIExpression::ExprOperand> peekNext() const {
    if (Start == End)
      return std::nullopt;

    auto Next = Start.getNext();
    if (Next == End)
      return std::nullopt;

    return *Next;
  }

  /// Peek at the operand N steps ahead without consuming.
  /// \param N Count, index, or node being visited.
  /// \return peek at the operand N steps ahead without consuming.
  std::optional<DIExpression::ExprOperand> peekNextN(unsigned N) const {
    if (Start == End)
      return std::nullopt;
    DIExpression::expr_op_iterator Nth = Start;
    for (unsigned I = 0; I < N; I++) {
      Nth = Nth.getNext();
      if (Nth == End)
        return std::nullopt;
    }
    return *Nth;
  }

  /// Replace the expression this cursor walks.
  /// \param Expr DIExpression to transform or query.
  void assignNewExpr(ArrayRef<uint64_t> Expr) {
    this->Start = DIExpression::expr_op_iterator(Expr.begin());
    this->End = DIExpression::expr_op_iterator(Expr.end());
  }

  /// Determine whether there are any operations left in this expression.
  /// \return determine whether there are any operations left in this expression.
  operator bool() const { return Start != End; }

  /// Return an iterator to the beginning of the range.
  /// \return An iterator to the beginning of the range.
  DIExpression::expr_op_iterator begin() const { return Start; }
  /// Return an iterator to the end of the range.
  /// \return An iterator to the end of the range.
  DIExpression::expr_op_iterator end() const { return End; }

  /// Retrieve the fragment information, if any.
  /// \return retrieve the fragment information, if any.
  std::optional<DIExpression::FragmentInfo> getFragmentInfo() const {
    return DIExpression::getFragmentInfo(Start, End);
  }
};

/// Global variables.
///
/// TODO: Remove DisplayName.  It's always equal to Name.
class DIGlobalVariable : public DIVariable {
  friend class LLVMContextImpl;
  friend class MDNode;

  bool IsLocalToUnit;
  bool IsDefinition;

  DIGlobalVariable(LLVMContext &C, StorageType Storage, unsigned Line,
                   bool IsLocalToUnit, bool IsDefinition, uint32_t AlignInBits,
                   ArrayRef<Metadata *> Ops)
      : DIVariable(C, DIGlobalVariableKind, Storage, Line, Ops, AlignInBits),
        IsLocalToUnit(IsLocalToUnit), IsDefinition(IsDefinition) {}
  /// Destroy this DIGlobalVariable.
  ~DIGlobalVariable() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param LinkageName Mangled linkage name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param IsLocalToUnit Whether the symbol is local to its compile unit.
  /// \param IsDefinition Whether this is a definition.
  /// \param StaticDataMemberDeclaration Static data member declaration, if any.
  /// \param TemplateParams Template parameter list.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Annotations Annotation metadata tuple.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIGlobalVariable *
  getImpl(LLVMContext &Context, DIScope *Scope, StringRef Name,
          StringRef LinkageName, DIFile *File, unsigned Line, DIType *Type,
          bool IsLocalToUnit, bool IsDefinition,
          DIDerivedType *StaticDataMemberDeclaration, MDTuple *TemplateParams,
          uint32_t AlignInBits, DINodeArray Annotations, StorageType Storage,
          bool ShouldCreate = true) {
    return getImpl(Context, Scope, getCanonicalMDString(Context, Name),
                   getCanonicalMDString(Context, LinkageName), File, Line, Type,
                   IsLocalToUnit, IsDefinition, StaticDataMemberDeclaration,
                   cast_or_null<Metadata>(TemplateParams), AlignInBits,
                   Annotations.get(), Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param LinkageName Mangled linkage name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param IsLocalToUnit Whether the symbol is local to its compile unit.
  /// \param IsDefinition Whether this is a definition.
  /// \param StaticDataMemberDeclaration Static data member declaration, if any.
  /// \param TemplateParams Template parameter list.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Annotations Annotation metadata tuple.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIGlobalVariable *
  getImpl(LLVMContext &Context, Metadata *Scope, MDString *Name,
          MDString *LinkageName, Metadata *File, unsigned Line, Metadata *Type,
          bool IsLocalToUnit, bool IsDefinition,
          Metadata *StaticDataMemberDeclaration, Metadata *TemplateParams,
          uint32_t AlignInBits, Metadata *Annotations, StorageType Storage,
          bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIGlobalVariable cloneImpl() const {
    return getTemporary(getContext(), getScope(), getName(), getLinkageName(),
                        getFile(), getLine(), getType(), isLocalToUnit(),
                        isDefinition(), getStaticDataMemberDeclaration(),
                        getTemplateParams(), getAlignInBits(),
                        getAnnotations());
  }

public:
  /// Get or create a DIGlobalVariable with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param LinkageName Mangled linkage name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param IsLocalToUnit Whether the symbol is local to its compile unit.
  /// \param IsDefinition Whether this is a definition.
  /// \param StaticDataMemberDeclaration Static data member declaration, if any.
  /// \param TemplateParams Template parameter list.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Annotations Annotation metadata tuple.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(
      DIGlobalVariable,
      (DIScope * Scope, StringRef Name, StringRef LinkageName, DIFile *File,
       unsigned Line, DIType *Type, bool IsLocalToUnit, bool IsDefinition,
       DIDerivedType *StaticDataMemberDeclaration, MDTuple *TemplateParams,
       uint32_t AlignInBits, DINodeArray Annotations),
      (Scope, Name, LinkageName, File, Line, Type, IsLocalToUnit, IsDefinition,
       StaticDataMemberDeclaration, TemplateParams, AlignInBits, Annotations))
  /// Get or create a DIGlobalVariable with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param LinkageName Mangled linkage name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param IsLocalToUnit Whether the symbol is local to its compile unit.
  /// \param IsDefinition Whether this is a definition.
  /// \param StaticDataMemberDeclaration Static data member declaration, if any.
  /// \param TemplateParams Template parameter list.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Annotations Annotation metadata tuple.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(
      DIGlobalVariable,
      (Metadata * Scope, MDString *Name, MDString *LinkageName, Metadata *File,
       unsigned Line, Metadata *Type, bool IsLocalToUnit, bool IsDefinition,
       Metadata *StaticDataMemberDeclaration, Metadata *TemplateParams,
       uint32_t AlignInBits, Metadata *Annotations),
      (Scope, Name, LinkageName, File, Line, Type, IsLocalToUnit, IsDefinition,
       StaticDataMemberDeclaration, TemplateParams, AlignInBits, Annotations))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIGlobalVariable clone() const { return cloneImpl(); }

  /// Return true if this is local to unit.
  /// \return true if this is local to unit.
  bool isLocalToUnit() const { return IsLocalToUnit; }
  /// Return true if this is definition.
  /// \return true if this is definition.
  bool isDefinition() const { return IsDefinition; }
  /// Return the display name.
  /// \return The display name.
  StringRef getDisplayName() const { return getStringOperand(4); }
  /// Return the linkage name.
  /// \return The linkage name.
  StringRef getLinkageName() const { return getStringOperand(5); }
  /// Return the static data member declaration.
  /// \return The static data member declaration.
  DIDerivedType *getStaticDataMemberDeclaration() const {
    return cast_or_null<DIDerivedType>(getRawStaticDataMemberDeclaration());
  }
  /// Return the annotations.
  /// \return The annotations.
  DINodeArray getAnnotations() const {
    return cast_or_null<MDTuple>(getRawAnnotations());
  }

  /// Return the raw linkage name operand.
  /// \return The raw linkage name operand.
  MDString *getRawLinkageName() const { return getOperandAs<MDString>(5); }
  /// Return the raw static data member declaration operand.
  /// \return The raw static data member declaration operand.
  Metadata *getRawStaticDataMemberDeclaration() const { return getOperand(6); }
  /// Return the raw template params operand.
  /// \return The raw template params operand.
  Metadata *getRawTemplateParams() const { return getOperand(7); }
  /// Return the template params.
  /// \return The template params.
  MDTuple *getTemplateParams() const { return getOperandAs<MDTuple>(7); }
  /// Return the raw annotations operand.
  /// \return The raw annotations operand.
  Metadata *getRawAnnotations() const { return getOperand(8); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIGlobalVariableKind;
  }
};

/// Debug common block.
///
/// Uses the SubclassData32 Metadata slot.
class DICommonBlock : public DIScope {
  friend class LLVMContextImpl;
  friend class MDNode;

  /// DI Common Block.
  /// \param Context LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param LineNo Source line number.
  /// \param Ops Operand list, or expression opcodes.
  DICommonBlock(LLVMContext &Context, StorageType Storage, unsigned LineNo,
                ArrayRef<Metadata *> Ops);

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Decl The decl.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DICommonBlock *getImpl(LLVMContext &Context, DIScope *Scope,
                                DIGlobalVariable *Decl, StringRef Name,
                                DIFile *File, unsigned LineNo,
                                StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Scope, Decl, getCanonicalMDString(Context, Name),
                   File, LineNo, Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Decl The decl.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DICommonBlock *getImpl(LLVMContext &Context, Metadata *Scope,
                                         Metadata *Decl, MDString *Name,
                                         Metadata *File, unsigned LineNo,
                                         StorageType Storage,
                                         bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDICommonBlock cloneImpl() const {
    return getTemporary(getContext(), getScope(), getDecl(), getName(),
                        getFile(), getLineNo());
  }

public:
  /// Get or create a DICommonBlock with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Decl Declaration metadata associated with the common block.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DICommonBlock,
                    (DIScope * Scope, DIGlobalVariable *Decl, StringRef Name,
                     DIFile *File, unsigned LineNo),
                    (Scope, Decl, Name, File, LineNo))
  /// Get or create a DICommonBlock with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Decl Declaration metadata associated with the common block.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param LineNo Source line number.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DICommonBlock,
                    (Metadata * Scope, Metadata *Decl, MDString *Name,
                     Metadata *File, unsigned LineNo),
                    (Scope, Decl, Name, File, LineNo))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDICommonBlock clone() const { return cloneImpl(); }

  /// Return the scope.
  /// \return The scope.
  DIScope *getScope() const { return cast_or_null<DIScope>(getRawScope()); }
  /// Return the decl.
  /// \return The decl.
  DIGlobalVariable *getDecl() const {
    return cast_or_null<DIGlobalVariable>(getRawDecl());
  }
  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(2); }
  /// Return the file.
  /// \return The file.
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }
  /// Return the line no.
  /// \return The line no.
  unsigned getLineNo() const { return SubclassData32; }

  /// Return the raw scope operand.
  /// \return The raw scope operand.
  Metadata *getRawScope() const { return getOperand(0); }
  /// Return the raw decl operand.
  /// \return The raw decl operand.
  Metadata *getRawDecl() const { return getOperand(1); }
  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(2); }
  /// Return the raw file operand.
  /// \return The raw file operand.
  Metadata *getRawFile() const { return getOperand(3); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DICommonBlockKind;
  }
};

/// Local variable.
///
/// TODO: Split up flags.
class DILocalVariable : public DIVariable {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Arg : 16;
  DIFlags Flags;

  DILocalVariable(LLVMContext &C, StorageType Storage, unsigned Line,
                  unsigned Arg, DIFlags Flags, uint32_t AlignInBits,
                  ArrayRef<Metadata *> Ops)
      : DIVariable(C, DILocalVariableKind, Storage, Line, Ops, AlignInBits),
        Arg(Arg), Flags(Flags) {
    assert(Arg < (1 << 16) && "DILocalVariable: Arg out of range");
  }
  /// Destroy this DILocalVariable.
  ~DILocalVariable() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param Arg Parameter argument number, or 0 if not a parameter.
  /// \param Flags Flags bitfield.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Annotations Annotation metadata tuple.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DILocalVariable *getImpl(LLVMContext &Context, DIScope *Scope,
                                  StringRef Name, DIFile *File, unsigned Line,
                                  DIType *Type, unsigned Arg, DIFlags Flags,
                                  uint32_t AlignInBits, DINodeArray Annotations,
                                  StorageType Storage,
                                  bool ShouldCreate = true) {
    return getImpl(Context, Scope, getCanonicalMDString(Context, Name), File,
                   Line, Type, Arg, Flags, AlignInBits, Annotations.get(),
                   Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param Arg Parameter argument number, or 0 if not a parameter.
  /// \param Flags Flags bitfield.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Annotations Annotation metadata tuple.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DILocalVariable *
  getImpl(LLVMContext &Context, Metadata *Scope, MDString *Name, Metadata *File,
          unsigned Line, Metadata *Type, unsigned Arg, DIFlags Flags,
          uint32_t AlignInBits, Metadata *Annotations, StorageType Storage,
          bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDILocalVariable cloneImpl() const {
    return getTemporary(getContext(), getScope(), getName(), getFile(),
                        getLine(), getType(), getArg(), getFlags(),
                        getAlignInBits(), getAnnotations());
  }

public:
  /// Get or create a DILocalVariable with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param Arg Parameter argument number, or 0 if not a parameter.
  /// \param Flags Flags bitfield.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Annotations Annotation metadata tuple.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DILocalVariable,
                    (DILocalScope * Scope, StringRef Name, DIFile *File,
                     unsigned Line, DIType *Type, unsigned Arg, DIFlags Flags,
                     uint32_t AlignInBits, DINodeArray Annotations),
                    (Scope, Name, File, Line, Type, Arg, Flags, AlignInBits,
                     Annotations))
  /// Get or create a DILocalVariable with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param Arg Parameter argument number, or 0 if not a parameter.
  /// \param Flags Flags bitfield.
  /// \param AlignInBits ABI alignment in bits.
  /// \param Annotations Annotation metadata tuple.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DILocalVariable,
                    (Metadata * Scope, MDString *Name, Metadata *File,
                     unsigned Line, Metadata *Type, unsigned Arg, DIFlags Flags,
                     uint32_t AlignInBits, Metadata *Annotations),
                    (Scope, Name, File, Line, Type, Arg, Flags, AlignInBits,
                     Annotations))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDILocalVariable clone() const { return cloneImpl(); }

  /// Get the local scope for this variable.
  ///
  /// Variables must be defined in a local scope.
  /// \return get the local scope for this variable.
  DILocalScope *getScope() const {
    return cast<DILocalScope>(DIVariable::getScope());
  }

  /// Return true if this is parameter.
  /// \return true if this is parameter.
  bool isParameter() const { return Arg; }
  /// Return the arg.
  /// \return The arg.
  unsigned getArg() const { return Arg; }
  /// Return the flags.
  /// \return The flags.
  DIFlags getFlags() const { return Flags; }

  /// Return the annotations.
  /// \return The annotations.
  DINodeArray getAnnotations() const {
    return cast_or_null<MDTuple>(getRawAnnotations());
  }
  /// Return the raw annotations operand.
  /// \return The raw annotations operand.
  Metadata *getRawAnnotations() const { return getOperand(4); }

  /// Return true if this is artificial.
  /// \return true if this is artificial.
  bool isArtificial() const { return getFlags() & FlagArtificial; }
  /// Return true if this is object pointer.
  /// \return true if this is object pointer.
  bool isObjectPointer() const { return getFlags() & FlagObjectPointer; }

  /// Check that a location is valid for this variable.
  ///
  /// Check that \c DL exists, is in the same subprogram, and has the same
  /// inlined-at location as \c this.  (Otherwise, it's not a valid attachment
  /// to a \a DbgInfoIntrinsic.)
  /// \param DL Debug location used for validation or fragment math.
  /// \return check that a location is valid for this variable.
  bool isValidLocationForIntrinsic(const DILocation *DL) const {
    return DL && getScope()->getSubprogram() == DL->getScope()->getSubprogram();
  }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILocalVariableKind;
  }
};

/// Label.
///
/// Uses the SubclassData32 Metadata slot.
class DILabel : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Column;
  std::optional<unsigned> CoroSuspendIdx;
  bool IsArtificial;

  /// DI Label.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param IsArtificial The is artificial.
  /// \param CoroSuspendIdx The coro suspend idx.
  /// \param Ops Operand list, or expression opcodes.
  DILabel(LLVMContext &C, StorageType Storage, unsigned Line, unsigned Column,
          bool IsArtificial, std::optional<unsigned> CoroSuspendIdx,
          ArrayRef<Metadata *> Ops);
  /// Destroy this DILabel.
  ~DILabel() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param IsArtificial The is artificial.
  /// \param CoroSuspendIdx The coro suspend idx.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DILabel *getImpl(LLVMContext &Context, DIScope *Scope, StringRef Name,
                          DIFile *File, unsigned Line, unsigned Column,
                          bool IsArtificial,
                          std::optional<unsigned> CoroSuspendIdx,
                          StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Scope, getCanonicalMDString(Context, Name), File,
                   Line, Column, IsArtificial, CoroSuspendIdx, Storage,
                   ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param IsArtificial The is artificial.
  /// \param CoroSuspendIdx The coro suspend idx.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DILabel *
  getImpl(LLVMContext &Context, Metadata *Scope, MDString *Name, Metadata *File,
          unsigned Line, unsigned Column, bool IsArtificial,
          std::optional<unsigned> CoroSuspendIdx, StorageType Storage,
          bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDILabel cloneImpl() const {
    return getTemporary(getContext(), getScope(), getName(), getFile(),
                        getLine(), getColumn(), isArtificial(),
                        getCoroSuspendIdx());
  }

public:
  /// Get or create a DILabel with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param IsArtificial Whether the label is artificial.
  /// \param CoroSuspendIdx Coroutine suspend index, if applicable.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DILabel,
                    (DILocalScope * Scope, StringRef Name, DIFile *File,
                     unsigned Line, unsigned Column, bool IsArtificial,
                     std::optional<unsigned> CoroSuspendIdx),
                    (Scope, Name, File, Line, Column, IsArtificial,
                     CoroSuspendIdx))
  /// Get or create a DILabel with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Scope Parent lexical or type scope.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param IsArtificial Whether the label is artificial.
  /// \param CoroSuspendIdx Coroutine suspend index, if applicable.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DILabel,
                    (Metadata * Scope, MDString *Name, Metadata *File,
                     unsigned Line, unsigned Column, bool IsArtificial,
                     std::optional<unsigned> CoroSuspendIdx),
                    (Scope, Name, File, Line, Column, IsArtificial,
                     CoroSuspendIdx))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDILabel clone() const { return cloneImpl(); }

  /// Get the local scope for this label.
  ///
  /// Labels must be defined in a local scope.
  /// \return get the local scope for this label.
  DILocalScope *getScope() const {
    return cast_or_null<DILocalScope>(getRawScope());
  }
  /// Return the line.
  /// \return The line.
  unsigned getLine() const { return SubclassData32; }
  /// Return the column.
  /// \return The column.
  unsigned getColumn() const { return Column; }
  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(1); }
  /// Return the file.
  /// \return The file.
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }
  /// Return true if this is artificial.
  /// \return true if this is artificial.
  bool isArtificial() const { return IsArtificial; }
  /// Return the coro suspend idx.
  /// \return The coro suspend idx.
  std::optional<unsigned> getCoroSuspendIdx() const { return CoroSuspendIdx; }

  /// Return the raw scope operand.
  /// \return The raw scope operand.
  Metadata *getRawScope() const { return getOperand(0); }
  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(1); }
  /// Return the raw file operand.
  /// \return The raw file operand.
  Metadata *getRawFile() const { return getOperand(2); }

  /// Check that a location is valid for this label.
  ///
  /// Check that \c DL exists, is in the same subprogram, and has the same
  /// inlined-at location as \c this.  (Otherwise, it's not a valid attachment
  /// to a \a DbgInfoIntrinsic.)
  /// \param DL Debug location used for validation or fragment math.
  /// \return check that a location is valid for this label.
  bool isValidLocationForIntrinsic(const DILocation *DL) const {
    return DL && getScope()->getSubprogram() == DL->getScope()->getSubprogram();
  }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILabelKind;
  }
};

/// Objective-C property.
class DIObjCProperty : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Line;
  unsigned Attributes;

  /// DI Obj C Property.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Line Source line number.
  /// \param Attributes Objective-C property attributes bitfield.
  /// \param Ops Operand list, or expression opcodes.
  DIObjCProperty(LLVMContext &C, StorageType Storage, unsigned Line,
                 unsigned Attributes, ArrayRef<Metadata *> Ops);
  /// Destroy this DIObjCProperty.
  ~DIObjCProperty() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param GetterName Objective-C getter selector name.
  /// \param SetterName Objective-C setter selector name.
  /// \param Attributes Objective-C property attributes bitfield.
  /// \param Type Type metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIObjCProperty *
  getImpl(LLVMContext &Context, StringRef Name, DIFile *File, unsigned Line,
          StringRef GetterName, StringRef SetterName, unsigned Attributes,
          DIType *Type, StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, getCanonicalMDString(Context, Name), File, Line,
                   getCanonicalMDString(Context, GetterName),
                   getCanonicalMDString(Context, SetterName), Attributes, Type,
                   Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param GetterName Objective-C getter selector name.
  /// \param SetterName Objective-C setter selector name.
  /// \param Attributes Objective-C property attributes bitfield.
  /// \param Type Type metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIObjCProperty *
  getImpl(LLVMContext &Context, MDString *Name, Metadata *File, unsigned Line,
          MDString *GetterName, MDString *SetterName, unsigned Attributes,
          Metadata *Type, StorageType Storage, bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIObjCProperty cloneImpl() const {
    return getTemporary(getContext(), getName(), getFile(), getLine(),
                        getGetterName(), getSetterName(), getAttributes(),
                        getType());
  }

public:
  /// Get or create a DIObjCProperty with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param GetterName Objective-C getter selector name.
  /// \param SetterName Objective-C setter selector name.
  /// \param Attributes Objective-C property attributes bitfield.
  /// \param Type Type metadata.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIObjCProperty,
                    (StringRef Name, DIFile *File, unsigned Line,
                     StringRef GetterName, StringRef SetterName,
                     unsigned Attributes, DIType *Type),
                    (Name, File, Line, GetterName, SetterName, Attributes,
                     Type))
  /// Get or create a DIObjCProperty with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param GetterName Objective-C getter selector name.
  /// \param SetterName Objective-C setter selector name.
  /// \param Attributes Objective-C property attributes bitfield.
  /// \param Type Type metadata.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIObjCProperty,
                    (MDString * Name, Metadata *File, unsigned Line,
                     MDString *GetterName, MDString *SetterName,
                     unsigned Attributes, Metadata *Type),
                    (Name, File, Line, GetterName, SetterName, Attributes,
                     Type))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIObjCProperty clone() const { return cloneImpl(); }

  /// Return the line.
  /// \return The line.
  unsigned getLine() const { return Line; }
  /// Return the attributes.
  /// \return The attributes.
  unsigned getAttributes() const { return Attributes; }
  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(0); }
  /// Return the file.
  /// \return The file.
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }
  /// Return the getter name.
  /// \return The getter name.
  StringRef getGetterName() const { return getStringOperand(2); }
  /// Return the setter name.
  /// \return The setter name.
  StringRef getSetterName() const { return getStringOperand(3); }
  /// Return the type.
  /// \return The type.
  DIType *getType() const { return cast_or_null<DIType>(getRawType()); }

  /// Return the filename.
  /// \return The filename.
  StringRef getFilename() const {
    if (auto *F = getFile())
      return F->getFilename();
    return "";
  }

  /// Return the directory.
  /// \return The directory.
  StringRef getDirectory() const {
    if (auto *F = getFile())
      return F->getDirectory();
    return "";
  }

  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(0); }
  /// Return the raw file operand.
  /// \return The raw file operand.
  Metadata *getRawFile() const { return getOperand(1); }
  /// Return the raw getter name operand.
  /// \return The raw getter name operand.
  MDString *getRawGetterName() const { return getOperandAs<MDString>(2); }
  /// Return the raw setter name operand.
  /// \return The raw setter name operand.
  MDString *getRawSetterName() const { return getOperandAs<MDString>(3); }
  /// Return the raw type operand.
  /// \return The raw type operand.
  Metadata *getRawType() const { return getOperand(4); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIObjCPropertyKind;
  }
};

/// A property of a class or structure.
///
/// An entity that is syntactically accessed like a data member, but whose
/// access is implemented by invoking a user-defined or compiler-generated
/// accessor.
///
/// Currently only the backing storage is modelled, and it must be a data
/// member holding the property's storage.
class DIProperty : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Line;

  /// DI Property.
  /// \param C LLVM context that owns the metadata.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param Line Source line number.
  /// \param Ops Operand list, or expression opcodes.
  DIProperty(LLVMContext &C, StorageType Storage, unsigned Line,
             ArrayRef<Metadata *> Ops);
  /// Destroy this DIProperty.
  ~DIProperty() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param BackingStorage The backing storage.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIProperty *getImpl(LLVMContext &Context, StringRef Name, DIFile *File,
                             unsigned Line, DIType *Type,
                             DINode *BackingStorage, StorageType Storage,
                             bool ShouldCreate = true) {
    return getImpl(Context, getCanonicalMDString(Context, Name), File, Line,
                   Type, BackingStorage, Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param BackingStorage The backing storage.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIProperty *getImpl(LLVMContext &Context, MDString *Name,
                                      Metadata *File, unsigned Line,
                                      Metadata *Type, Metadata *BackingStorage,
                                      StorageType Storage,
                                      bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIProperty cloneImpl() const {
    return getTemporary(getContext(), getName(), getFile(), getLine(),
                        getType(), getBackingStorage());
  }

public:
  /// Get or create a DIProperty with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param BackingStorage Backing storage for the property.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIProperty,
                    (StringRef Name, DIFile *File, unsigned Line, DIType *Type,
                     DINode *BackingStorage),
                    (Name, File, Line, Type, BackingStorage))
  /// Get or create a DIProperty with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Name Source-level name.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Type Type metadata.
  /// \param BackingStorage Backing storage for the property.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIProperty,
                    (MDString * Name, Metadata *File, unsigned Line,
                     Metadata *Type, Metadata *BackingStorage),
                    (Name, File, Line, Type, BackingStorage))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIProperty clone() const { return cloneImpl(); }

  /// Return the line.
  /// \return The line.
  unsigned getLine() const { return Line; }
  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(0); }
  /// Return the file.
  /// \return The file.
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }
  /// Return the type.
  /// \return The type.
  DIType *getType() const { return cast_or_null<DIType>(getRawType()); }

  /// The data member holding the property's backing storage, i.e. the target
  /// of \c DW_AT_property_forward on this property's
  /// \c DW_TAG_property_getter child.
  /// \return the data member holding the property's backing storage, i.e. the target.
  DINode *getBackingStorage() const {
    return cast_or_null<DINode>(getRawBackingStorage());
  }

  /// Return the filename.
  /// \return The filename.
  StringRef getFilename() const {
    if (auto *F = getFile())
      return F->getFilename();
    return "";
  }

  /// Return the directory.
  /// \return The directory.
  StringRef getDirectory() const {
    if (auto *F = getFile())
      return F->getDirectory();
    return "";
  }

  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(0); }
  /// Return the raw file operand.
  /// \return The raw file operand.
  Metadata *getRawFile() const { return getOperand(1); }
  /// Return the raw type operand.
  /// \return The raw type operand.
  Metadata *getRawType() const { return getOperand(2); }
  /// Return the raw backing storage operand.
  /// \return The raw backing storage operand.
  Metadata *getRawBackingStorage() const { return getOperand(3); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIPropertyKind;
  }
};

/// An imported module (C++ using directive or similar).
///
/// Uses the SubclassData32 Metadata slot.
class DIImportedEntity : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  DIImportedEntity(LLVMContext &C, StorageType Storage, unsigned Tag,
                   unsigned Line, ArrayRef<Metadata *> Ops)
      : DINode(C, DIImportedEntityKind, Storage, Tag, Ops) {
    SubclassData32 = Line;
  }
  /// Destroy this DIImportedEntity.
  ~DIImportedEntity() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Scope Parent lexical or type scope.
  /// \param Entity Imported entity.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Name Source-level name.
  /// \param Elements Elements array or expression element words.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIImportedEntity *getImpl(LLVMContext &Context, unsigned Tag,
                                   DIScope *Scope, DINode *Entity, DIFile *File,
                                   unsigned Line, StringRef Name,
                                   DINodeArray Elements, StorageType Storage,
                                   bool ShouldCreate = true) {
    return getImpl(Context, Tag, Scope, Entity, File, Line,
                   getCanonicalMDString(Context, Name), Elements.get(), Storage,
                   ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Scope Parent lexical or type scope.
  /// \param Entity Imported entity.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Name Source-level name.
  /// \param Elements Elements array or expression element words.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIImportedEntity *
  getImpl(LLVMContext &Context, unsigned Tag, Metadata *Scope, Metadata *Entity,
          Metadata *File, unsigned Line, MDString *Name, Metadata *Elements,
          StorageType Storage, bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIImportedEntity cloneImpl() const {
    return getTemporary(getContext(), getTag(), getScope(), getEntity(),
                        getFile(), getLine(), getName(), getElements());
  }

public:
  /// Get or create a DIImportedEntity with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Scope Parent lexical or type scope.
  /// \param Entity Imported entity.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Name Source-level name.
  /// \param Elements Elements array or expression element words.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIImportedEntity,
                    (unsigned Tag, DIScope *Scope, DINode *Entity, DIFile *File,
                     unsigned Line, StringRef Name = "",
                     DINodeArray Elements = nullptr),
                    (Tag, Scope, Entity, File, Line, Name, Elements))
  /// Get or create a DIImportedEntity with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Tag DWARF tag for the node.
  /// \param Scope Parent lexical or type scope.
  /// \param Entity Imported entity.
  /// \param File Source file metadata.
  /// \param Line Source line number.
  /// \param Name Source-level name.
  /// \param Elements Elements array or expression element words.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIImportedEntity,
                    (unsigned Tag, Metadata *Scope, Metadata *Entity,
                     Metadata *File, unsigned Line, MDString *Name,
                     Metadata *Elements = nullptr),
                    (Tag, Scope, Entity, File, Line, Name, Elements))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIImportedEntity clone() const { return cloneImpl(); }

  /// Return the line.
  /// \return The line.
  unsigned getLine() const { return SubclassData32; }
  /// Return the scope.
  /// \return The scope.
  DIScope *getScope() const { return cast_or_null<DIScope>(getRawScope()); }
  /// Return the entity.
  /// \return The entity.
  DINode *getEntity() const { return cast_or_null<DINode>(getRawEntity()); }
  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(2); }
  /// Return the file.
  /// \return The file.
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }
  /// Return the elements.
  /// \return The elements.
  DINodeArray getElements() const {
    return cast_or_null<MDTuple>(getRawElements());
  }

  /// Return the raw scope operand.
  /// \return The raw scope operand.
  Metadata *getRawScope() const { return getOperand(0); }
  /// Return the raw entity operand.
  /// \return The raw entity operand.
  Metadata *getRawEntity() const { return getOperand(1); }
  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(2); }
  /// Return the raw file operand.
  /// \return The raw file operand.
  Metadata *getRawFile() const { return getOperand(3); }
  /// Return the raw elements operand.
  /// \return The raw elements operand.
  Metadata *getRawElements() const { return getOperand(4); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIImportedEntityKind;
  }
};

/// A pair of DIGlobalVariable and DIExpression.
class DIGlobalVariableExpression : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  DIGlobalVariableExpression(LLVMContext &C, StorageType Storage,
                             ArrayRef<Metadata *> Ops)
      : MDNode(C, DIGlobalVariableExpressionKind, Storage, Ops) {}
  /// Destroy this DIGlobalVariableExpression.
  ~DIGlobalVariableExpression() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param Variable The variable.
  /// \param Expression The expression.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIGlobalVariableExpression *
  getImpl(LLVMContext &Context, Metadata *Variable, Metadata *Expression,
          StorageType Storage, bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIGlobalVariableExpression cloneImpl() const {
    return getTemporary(getContext(), getVariable(), getExpression());
  }

public:
  /// Get or create a DIGlobalVariableExpression with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param Variable Global variable metadata.
  /// \param Expression DIExpression attached to the variable.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIGlobalVariableExpression,
                    (Metadata * Variable, Metadata *Expression),
                    (Variable, Expression))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIGlobalVariableExpression clone() const { return cloneImpl(); }

  /// Return the raw variable operand.
  /// \return The raw variable operand.
  Metadata *getRawVariable() const { return getOperand(0); }

  /// Return the variable.
  /// \return The variable.
  DIGlobalVariable *getVariable() const {
    return cast_or_null<DIGlobalVariable>(getRawVariable());
  }

  /// Return the raw expression operand.
  /// \return The raw expression operand.
  Metadata *getRawExpression() const { return getOperand(1); }

  /// Return the expression.
  /// \return The expression.
  DIExpression *getExpression() const {
    return cast<DIExpression>(getRawExpression());
  }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIGlobalVariableExpressionKind;
  }
};

/// Macro Info DWARF-like metadata node.
///
/// A metadata node with a DWARF macro info (i.e., a constant named
/// \c DW_MACINFO_*, defined in llvm/BinaryFormat/Dwarf.h).  Called \a
/// DIMacroNode
/// because it's potentially used for non-DWARF output.
///
/// Uses the SubclassData16 Metadata slot.
class DIMacroNode : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

protected:
  /// Construct a DIMacroNode.
  /// \param C LLVM context that owns the metadata.
  /// \param ID Metadata subclass ID.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param MIType DWARF macro info type code.
  /// \param Ops1 First operand list.
  /// \param Ops2 Optional second operand list.
  DIMacroNode(LLVMContext &C, unsigned ID, StorageType Storage, unsigned MIType,
              ArrayRef<Metadata *> Ops1, ArrayRef<Metadata *> Ops2 = {})
      : MDNode(C, ID, Storage, Ops1, Ops2) {
    assert(MIType < 1u << 16);
    SubclassData16 = MIType;
  }
  /// Destroy this DIMacroNode.
  ~DIMacroNode() = default;

  /// Return the operand as.
  /// \param I Operand or argument index.
  /// \return The operand as.
  template <class Ty> Ty *getOperandAs(unsigned I) const {
    return cast_or_null<Ty>(getOperand(I));
  }

  /// Return the string operand.
  /// \param I Operand or argument index.
  /// \return The string operand.
  StringRef getStringOperand(unsigned I) const {
    if (auto *S = getOperandAs<MDString>(I))
      return S->getString();
    return StringRef();
  }

  /// Return the canonical md string.
  /// \param Context LLVM context that owns the metadata.
  /// \param S String to intern as MDString.
  /// \return The canonical md string.
  static MDString *getCanonicalMDString(LLVMContext &Context, StringRef S) {
    if (S.empty())
      return nullptr;
    return MDString::get(Context, S);
  }

public:
  /// Return the macinfo type.
  /// \return The macinfo type.
  unsigned getMacinfoType() const { return SubclassData16; }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    switch (MD->getMetadataID()) {
    default:
      return false;
    case DIMacroKind:
    case DIMacroFileKind:
      return true;
    }
  }
};

/// Macro
///
/// Uses the SubclassData32 Metadata slot.
class DIMacro : public DIMacroNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  DIMacro(LLVMContext &C, StorageType Storage, unsigned MIType, unsigned Line,
          ArrayRef<Metadata *> Ops)
      : DIMacroNode(C, DIMacroKind, Storage, MIType, Ops) {
    SubclassData32 = Line;
  }
  /// Destroy this DIMacro.
  ~DIMacro() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param MIType The mi type.
  /// \param Line Source line number.
  /// \param Name Source-level name.
  /// \param Value Enumerator or template value.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIMacro *getImpl(LLVMContext &Context, unsigned MIType, unsigned Line,
                          StringRef Name, StringRef Value, StorageType Storage,
                          bool ShouldCreate = true) {
    return getImpl(Context, MIType, Line, getCanonicalMDString(Context, Name),
                   getCanonicalMDString(Context, Value), Storage, ShouldCreate);
  }
  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param MIType The mi type.
  /// \param Line Source line number.
  /// \param Name Source-level name.
  /// \param Value Enumerator or template value.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIMacro *getImpl(LLVMContext &Context, unsigned MIType,
                                   unsigned Line, MDString *Name,
                                   MDString *Value, StorageType Storage,
                                   bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIMacro cloneImpl() const {
    return getTemporary(getContext(), getMacinfoType(), getLine(), getName(),
                        getValue());
  }

public:
  /// Get or create a DIMacro with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param MIType Macro info type (define or undef).
  /// \param Line Source line number.
  /// \param Name Source-level name.
  /// \param Value Enumerator or template value.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIMacro,
                    (unsigned MIType, unsigned Line, StringRef Name,
                     StringRef Value = ""),
                    (MIType, Line, Name, Value))
  /// Get or create a DIMacro with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param MIType Macro info type (define or undef).
  /// \param Line Source line number.
  /// \param Name Source-level name.
  /// \param Value Enumerator or template value.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIMacro,
                    (unsigned MIType, unsigned Line, MDString *Name,
                     MDString *Value),
                    (MIType, Line, Name, Value))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIMacro clone() const { return cloneImpl(); }

  /// Return the line.
  /// \return The line.
  unsigned getLine() const { return SubclassData32; }

  /// Return the name.
  /// \return The name.
  StringRef getName() const { return getStringOperand(0); }
  /// Return the value.
  /// \return The value.
  StringRef getValue() const { return getStringOperand(1); }

  /// Return the raw name operand.
  /// \return The raw name operand.
  MDString *getRawName() const { return getOperandAs<MDString>(0); }
  /// Return the raw value operand.
  /// \return The raw value operand.
  MDString *getRawValue() const { return getOperandAs<MDString>(1); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIMacroKind;
  }
};

/// Macro file
///
/// Uses the SubclassData32 Metadata slot.
class DIMacroFile : public DIMacroNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  DIMacroFile(LLVMContext &C, StorageType Storage, unsigned MIType,
              unsigned Line, ArrayRef<Metadata *> Ops)
      : DIMacroNode(C, DIMacroFileKind, Storage, MIType, Ops) {
    SubclassData32 = Line;
  }
  /// Destroy this DIMacroFile.
  ~DIMacroFile() = default;

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param MIType The mi type.
  /// \param Line Source line number.
  /// \param File Source file metadata.
  /// \param Elements Elements array or expression element words.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  static DIMacroFile *getImpl(LLVMContext &Context, unsigned MIType,
                              unsigned Line, DIFile *File,
                              DIMacroNodeArray Elements, StorageType Storage,
                              bool ShouldCreate = true) {
    return getImpl(Context, MIType, Line, static_cast<Metadata *>(File),
                   Elements.get(), Storage, ShouldCreate);
  }

  /// Get or create a node with the given operands.
  /// \param Context LLVM context that owns the metadata.
  /// \param MIType The mi type.
  /// \param Line Source line number.
  /// \param File Source file metadata.
  /// \param Elements Elements array or expression element words.
  /// \param Storage Storage/uniquing kind for the new node.
  /// \param ShouldCreate Whether to create the node if it does not exist.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIMacroFile *getImpl(LLVMContext &Context, unsigned MIType,
                                       unsigned Line, Metadata *File,
                                       Metadata *Elements, StorageType Storage,
                                       bool ShouldCreate = true);

  /// Clone this node into a temporary.
  /// \return A temporary clone of this node.
  TempDIMacroFile cloneImpl() const {
    return getTemporary(getContext(), getMacinfoType(), getLine(), getFile(),
                        getElements());
  }

public:
  /// Get or create a DIMacroFile with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param MIType Macro info type (define or undef).
  /// \param Line Source line number.
  /// \param File Source file metadata.
  /// \param Elements Elements array or expression element words.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIMacroFile,
                    (unsigned MIType, unsigned Line, DIFile *File,
                     DIMacroNodeArray Elements),
                    (MIType, Line, File, Elements))
  /// Get or create a DIMacroFile with the given operands.
  ///
  /// Also provides getIfExists, getDistinct, and getTemporary variants.
  /// \param Context LLVM context that owns the metadata.
  /// \param MIType Macro info type (define or undef).
  /// \param Line Source line number.
  /// \param File Source file metadata.
  /// \param Elements Elements array or expression element words.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  DEFINE_MDNODE_GET(DIMacroFile,
                    (unsigned MIType, unsigned Line, Metadata *File,
                     Metadata *Elements),
                    (MIType, Line, File, Elements))

  /// Return a (temporary) clone of this.
  /// \return A (temporary) clone of this.
  TempDIMacroFile clone() const { return cloneImpl(); }

  /// Replace the elements.
  /// \param Elements Elements array or expression element words.
  void replaceElements(DIMacroNodeArray Elements) {
#ifndef NDEBUG
    for (DIMacroNode *Op : getElements())
      assert(is_contained(Elements->operands(), Op) &&
             "Lost a macro node during macro node list replacement");
#endif
    replaceOperandWith(1, Elements.get());
  }

  /// Return the line.
  /// \return The line.
  unsigned getLine() const { return SubclassData32; }
  /// Return the file.
  /// \return The file.
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }

  /// Return the elements.
  /// \return The elements.
  DIMacroNodeArray getElements() const {
    return cast_or_null<MDTuple>(getRawElements());
  }

  /// Return the raw file operand.
  /// \return The raw file operand.
  Metadata *getRawFile() const { return getOperand(0); }
  /// Return the raw elements operand.
  /// \return The raw elements operand.
  Metadata *getRawElements() const { return getOperand(1); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIMacroFileKind;
  }
};

/// List of ValueAsMetadata, to be used as an argument to a dbg.value
/// intrinsic.
class DIArgList : public Metadata, ReplaceableMetadataImpl {
  friend class ReplaceableMetadataImpl;
  friend class LLVMContextImpl;
  /// iterator.
  using iterator = SmallVectorImpl<ValueAsMetadata *>::iterator;

  SmallVector<ValueAsMetadata *, 4> Args;

  DIArgList(LLVMContext &Context, ArrayRef<ValueAsMetadata *> Args)
      : Metadata(DIArgListKind, Uniqued), ReplaceableMetadataImpl(Context),
        Args(Args) {
    /// track.
    track();
  }
  /// Destroy this DIArgList.
  ~DIArgList() { untrack(); }

  /// track.
  LLVM_ABI void track();
  /// untrack.
  LLVM_ABI void untrack();
  /// Drop all references held by this DIArgList.
  /// \param Untrack Whether to untrack the dropped references.
  void dropAllReferences(bool Untrack);

public:
  /// Get or create a DIArgList for the given argument metadata.
  /// \param Context LLVM context that owns the metadata.
  /// \param Args Argument list for a DIArgList.
  /// \return The metadata node (uniqued, distinct, or temporary as requested); getIfExists may return nullptr.
  LLVM_ABI static DIArgList *get(LLVMContext &Context,
                                 ArrayRef<ValueAsMetadata *> Args);

  /// Return the args.
  /// \return The args.
  ArrayRef<ValueAsMetadata *> getArgs() const { return Args; }

  /// Return the args begin.
  /// \return The args begin.
  iterator args_begin() { return Args.begin(); }
  /// Return the args end.
  /// \return The args end.
  iterator args_end() { return Args.end(); }

  /// Check whether \p MD is this kind of metadata.
  /// \param MD Metadata node to test.
  /// \return true if \p MD is this kind of metadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIArgListKind;
  }

  /// Return the all dbg variable record users.
  /// \return All DbgVariableRecord users of this metadata.
  SmallVector<DbgVariableRecord *> getAllDbgVariableRecordUsers() {
    return ReplaceableMetadataImpl::getAllDbgVariableRecordUsers();
  }

  /// Update this node when a tracked operand changes.
  /// \param Ref Address of the tracking pointer that changed.
  /// \param New Replacement metadata value.
  LLVM_ABI void handleChangedOperand(void *Ref, Metadata *New);
};

/// Identifies a unique instance of a variable.
///
/// Storage for identifying a potentially inlined instance of a variable,
/// or a fragment thereof. This guarantees that exactly one variable instance
/// may be identified by this class, even when that variable is a fragment of
/// an aggregate variable and/or there is another inlined instance of the same
/// source code variable nearby.
/// This class does not necessarily uniquely identify that variable: it is
/// possible that a DebugVariable with different parameters may point to the
/// same variable instance, but not that one DebugVariable points to multiple
/// variable instances.
class DebugVariable {
  /// Fragment describing a bit-range within a variable.
  using FragmentInfo = DIExpression::FragmentInfo;

  const DILocalVariable *Variable;
  std::optional<FragmentInfo> Fragment;
  const DILocation *InlinedAt;

  /// Fragment that will overlap all other fragments. Used as default when
  /// caller demands a fragment.
  LLVM_ABI static const FragmentInfo DefaultFragment;

public:
  /// Construct a DebugVariable.
  /// \param DVR DbgVariableRecord describing the variable instance.
  LLVM_ABI DebugVariable(const DbgVariableRecord *DVR);

  /// Construct a DebugVariable from an explicit fragment.
  /// \param Var Local variable metadata.
  /// \param FragmentInfo Optional fragment describing a bit-range within the
  ///   variable.
  /// \param InlinedAt Inlined-at location, or nullptr if not inlined.
  DebugVariable(const DILocalVariable *Var,
                std::optional<FragmentInfo> FragmentInfo,
                const DILocation *InlinedAt)
      : Variable(Var), Fragment(FragmentInfo), InlinedAt(InlinedAt) {}

  /// Construct a DebugVariable, taking any fragment from \p DIExpr.
  /// \param Var Local variable metadata.
  /// \param DIExpr Expression that may carry fragment information.
  /// \param InlinedAt Inlined-at location, or nullptr if not inlined.
  DebugVariable(const DILocalVariable *Var, const DIExpression *DIExpr,
                const DILocation *InlinedAt)
      : Variable(Var),
        Fragment(DIExpr ? DIExpr->getFragmentInfo() : std::nullopt),
        InlinedAt(InlinedAt) {}

  /// Return the variable.
  /// \return The variable.
  const DILocalVariable *getVariable() const { return Variable; }
  /// Return the fragment.
  /// \return The fragment.
  std::optional<FragmentInfo> getFragment() const { return Fragment; }
  /// Return the inlined at.
  /// \return The inlined at.
  const DILocation *getInlinedAt() const { return InlinedAt; }

  /// Return the fragment or default.
  /// \return The fragment or default.
  FragmentInfo getFragmentOrDefault() const {
    return Fragment.value_or(DefaultFragment);
  }

  /// Return true if this is default fragment.
  /// \param F Fragment to test against the default fragment.
  /// \return true if this is default fragment.
  static bool isDefaultFragment(const FragmentInfo F) {
    return F == DefaultFragment;
  }

  /// Return true if the two values compare equal.
  /// \param Other Other fragment or expression to compare against.
  /// \return true if the two values compare equal.
  bool operator==(const DebugVariable &Other) const {
    return std::tie(Variable, Fragment, InlinedAt) ==
           std::tie(Other.Variable, Other.Fragment, Other.InlinedAt);
  }

  /// Return true if this value orders before the other.
  /// \param Other Other fragment or expression to compare against.
  /// \return true if this value orders before the other.
  bool operator<(const DebugVariable &Other) const {
    return std::tie(Variable, Fragment, InlinedAt) <
           std::tie(Other.Variable, Other.Fragment, Other.InlinedAt);
  }
};

/// DenseMapInfo specialization for DebugVariable keys.
template <> struct DenseMapInfo<DebugVariable> {
  /// Alias for a variable fragment descriptor.
  using FragmentInfo = DIExpression::FragmentInfo;

  /// Return the hash value.
  /// \param D Discriminator encoding or DenseMap key.
  /// \return The hash value.
  static unsigned getHashValue(const DebugVariable &D) {
    unsigned HV = 0;
    const std::optional<FragmentInfo> Fragment = D.getFragment();
    if (Fragment)
      HV = DenseMapInfo<FragmentInfo>::getHashValue(*Fragment);

    return hash_combine(D.getVariable(), HV, D.getInlinedAt());
  }

  /// Return true if this is equal.
  /// \param A Left-hand operand.
  /// \param B Right-hand operand.
  /// \return true if this is equal.
  static bool isEqual(const DebugVariable &A, const DebugVariable &B) {
    return A == B;
  }
};

/// Identifies a unique instance of a whole variable (discards/ignores fragment
/// information).
class DebugVariableAggregate : public DebugVariable {
public:
  /// Construct a DebugVariableAggregate.
  /// \param DVR DbgVariableRecord describing the variable instance.
  LLVM_ABI DebugVariableAggregate(const DbgVariableRecord *DVR);
  /// Construct an aggregate key from a DebugVariable, ignoring fragments.
  /// \param V Source debug variable whose fragment is discarded.
  DebugVariableAggregate(const DebugVariable &V)
      : DebugVariable(V.getVariable(), std::nullopt, V.getInlinedAt()) {}
};

/// DenseMapInfo specialization for DebugVariableAggregate keys.
template <>
struct DenseMapInfo<DebugVariableAggregate>
    : public DenseMapInfo<DebugVariable> {};

/// Return the lexical scope associated with \p N.
/// \param N Node that exposes a getScope() accessor.
/// \return The lexical scope associated with \p N.
template <typename NodeT> static const DIScope *getScope(const NodeT *N) {
  return N->getScope();
}

/// Return the lexical scope associated with \p N.
/// \param N Node that exposes a getScope() accessor.
/// \return The lexical scope associated with \p N.
template <typename NodeT> static DIScope *getScope(NodeT *N) {
  return N->getScope();
}

/// Return the scope of the global variable inside expression \p N.
/// \param N Global variable expression whose variable scope is returned.
/// \return The scope of the global variable inside expression \p N.
template <>
[[maybe_unused]] const DIScope *
getScope<>(const DIGlobalVariableExpression *N) {
  return N->getVariable()->getScope();
}
/// Return the scope of the global variable inside expression \p N.
/// \param N Global variable expression whose variable scope is returned.
/// \return The scope of the global variable inside expression \p N.
template <>
[[maybe_unused]] DIScope *getScope<>(DIGlobalVariableExpression *N) {
  return N->getVariable()->getScope();
}
} // end namespace llvm

#undef DEFINE_MDNODE_GET_UNPACK_IMPL
#undef DEFINE_MDNODE_GET_UNPACK
#undef DEFINE_MDNODE_GET

#endif // LLVM_IR_DEBUGINFOMETADATA_H
