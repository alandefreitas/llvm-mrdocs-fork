//===------------ JITLink.h - JIT linker functionality ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains generic JIT-linker types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_JITLINK_H
#define LLVM_EXECUTIONENGINE_JITLINK_JITLINK_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/Orc/Shared/MemoryFlags.h"
#include "llvm/ExecutionEngine/Orc/SymbolStringPool.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

#include <map>
#include <string>
#include <system_error>

namespace llvm {
namespace jitlink {

class LinkGraph;
class Symbol;
class Section;

/// Base class for errors originating in JIT linker, e.g. missing relocation
/// support.
class LLVM_ABI JITLinkError : public ErrorInfo<JITLinkError> {
public:
  /// ErrorInfo identifier for JITLinkError.
  static char ID;

  /// Construct a JITLink error with the given message.
  /// \param ErrMsg Human-readable error message.
  JITLinkError(Twine ErrMsg) : ErrMsg(ErrMsg.str()) {}

  /// Write this error's message to \p OS.
  /// \param OS Output stream to write to.
  void log(raw_ostream &OS) const override;
  /// Return the stored error message string.
  /// \return The stored error message string.
  const std::string &getErrorMessage() const { return ErrMsg; }
  /// Convert this error to a std::error_code.
  /// \return A std::error_code corresponding to this error.
  std::error_code convertToErrorCode() const override;

private:
  std::string ErrMsg;
};

/// Represents fixups and constraints in the LinkGraph.
class Edge {
public:
  /// Opaque edge-kind discriminator.
  using Kind = uint8_t;

  /// Generic edge kinds shared by all targets.
  enum GenericEdgeKind : Kind {
    Invalid,                    ///< Invalid edge value.
    FirstKeepAlive,             ///< Keeps target alive. Offset/addend zero.
    KeepAlive = FirstKeepAlive, ///< Tag first edge kind that preserves liveness.
    FirstRelocation             ///< First architecture specific relocation.
  };

  /// Offset of a fixup within its containing block.
  using OffsetT = uint32_t;
  /// Addend applied when evaluating an edge.
  using AddendT = int64_t;

  /// Construct an edge with the given kind, offset, target, and addend.
  /// \param K Edge kind.
  /// \param Offset Offset within the owning block.
  /// \param Target Symbol targeted by this edge.
  /// \param Addend Addend applied when fixing up.
  Edge(Kind K, OffsetT Offset, Symbol &Target, AddendT Addend)
      : Target(&Target), Offset(Offset), Addend(Addend), K(K) {}

  /// Return the offset of this edge within its block.
  /// \return The offset of this edge within its block.
  OffsetT getOffset() const { return Offset; }
  /// Set the offset of this edge within its block.
  /// \param Offset New offset within the owning block.
  void setOffset(OffsetT Offset) { this->Offset = Offset; }
  /// Return the kind of this edge.
  /// \return The kind of this edge.
  Kind getKind() const { return K; }
  /// Set the kind of this edge.
  /// \param K New edge kind.
  void setKind(Kind K) { this->K = K; }
  /// Return whether this edge is an architecture-specific relocation.
  /// \return True if this edge is an architecture-specific relocation.
  bool isRelocation() const { return K >= FirstRelocation; }
  /// Return the architecture-specific relocation index for this edge.
  /// \return The architecture-specific relocation index for this edge.
  Kind getRelocation() const {
    assert(isRelocation() && "Not a relocation edge");
    return K - FirstRelocation;
  }
  /// Return whether this edge preserves liveness of its target.
  /// \return True if this edge preserves liveness of its target.
  bool isKeepAlive() const { return K >= FirstKeepAlive; }
  /// Return the symbol targeted by this edge.
  /// \return The symbol targeted by this edge.
  Symbol &getTarget() const { return *Target; }
  /// Set the symbol targeted by this edge.
  /// \param Target New target symbol.
  void setTarget(Symbol &Target) { this->Target = &Target; }
  /// Return the addend applied when evaluating this edge.
  /// \return The addend applied when evaluating this edge.
  AddendT getAddend() const { return Addend; }
  /// Set the addend applied when evaluating this edge.
  /// \param Addend New addend value.
  void setAddend(AddendT Addend) { this->Addend = Addend; }

private:
  Symbol *Target = nullptr;
  OffsetT Offset = 0;
  AddendT Addend = 0;
  Kind K = 0;
};

/// Returns the string name of the given generic edge kind, or "unknown"
/// otherwise. Useful for debugging.
/// \param K Generic edge kind to name.
/// \return The name of \p K, or "unknown" if it is not a generic kind.
LLVM_ABI const char *getGenericEdgeKindName(Edge::Kind K);

/// Base class for Addressable entities (externals, absolutes, blocks).
class Addressable {
  friend class LinkGraph;

protected:
  /// Construct a defined or external addressable at \p Address.
  /// \param Address Address of this entity.
  /// \param IsDefined Whether this addressable is a defined block.
  Addressable(orc::ExecutorAddr Address, bool IsDefined)
      : Address(Address), IsDefined(IsDefined), IsAbsolute(false) {}

  /// Construct an absolute addressable at \p Address.
  /// \param Address Absolute address of this entity.
  Addressable(orc::ExecutorAddr Address)
      : Address(Address), IsDefined(false), IsAbsolute(true) {
    assert(!(IsDefined && IsAbsolute) &&
           "Block cannot be both defined and absolute");
  }

public:
  /// Deleted copy constructor; Addressable is non-copyable.
  /// \param Other Unused; copy construction is deleted.
  Addressable(const Addressable &Other) = delete;
  /// Default copy assignment for Addressable.
  /// \param Other Addressable to copy-assign from.
  /// \return A reference to this addressable.
  Addressable &operator=(const Addressable &Other) = default;
  /// Deleted move constructor; Addressable is non-movable.
  /// \param Other Unused; move construction is deleted.
  Addressable(Addressable &&Other) = delete;
  /// Default move assignment for Addressable.
  /// \param Other Addressable to move-assign from.
  /// \return A reference to this addressable.
  Addressable &operator=(Addressable &&Other) = default;

  /// Return the address of this entity.
  /// \return The address of this entity.
  orc::ExecutorAddr getAddress() const { return Address; }
  /// Set the address of this entity.
  /// \param Address New address for this entity.
  void setAddress(orc::ExecutorAddr Address) { this->Address = Address; }

  /// Returns true if this is a defined addressable, in which case you
  /// can downcast this to a Block.
  /// \return True if this is a defined addressable.
  bool isDefined() const { return static_cast<bool>(IsDefined); }
  /// Return whether this addressable is an absolute symbol.
  /// \return True if this addressable is an absolute symbol.
  bool isAbsolute() const { return static_cast<bool>(IsAbsolute); }

private:
  void setAbsolute(bool IsAbsolute) {
    assert(!IsDefined && "Cannot change the Absolute flag on a defined block");
    this->IsAbsolute = IsAbsolute;
  }

  orc::ExecutorAddr Address;
  uint64_t IsDefined : 1;
  uint64_t IsAbsolute : 1;

protected:
  // bitfields for Block, allocated here to improve packing.
  uint64_t ContentMutable : 1;   ///< Whether Block content is mutable.
  uint64_t P2Align : 5;          ///< Log2 of Block alignment.
  uint64_t AlignmentOffset : 56; ///< Alignment offset for Block content.
};

/// Ordinal used to order sections with the same permissions.
using SectionOrdinal = unsigned;

/// An Addressable with content and edges.
class Block : public Addressable {
  friend class LinkGraph;

private:
  /// Create a zero-fill defined addressable.
  Block(Section &Parent, orc::ExecutorAddrDiff Size, orc::ExecutorAddr Address,
        uint64_t Alignment, uint64_t AlignmentOffset)
      : Addressable(Address, true), Parent(&Parent), Size(Size) {
    assert(isPowerOf2_64(Alignment) && "Alignment must be power of 2");
    assert(AlignmentOffset < Alignment &&
           "Alignment offset cannot exceed alignment");
    assert(AlignmentOffset <= MaxAlignmentOffset &&
           "Alignment offset exceeds maximum");
    ContentMutable = false;
    P2Align = Alignment ? llvm::countr_zero(Alignment) : 0;
    this->AlignmentOffset = AlignmentOffset;
  }

  /// Create a defined addressable for the given content.
  /// The Content is assumed to be non-writable, and will be copied when
  /// mutations are required.
  Block(Section &Parent, ArrayRef<char> Content, orc::ExecutorAddr Address,
        uint64_t Alignment, uint64_t AlignmentOffset)
      : Addressable(Address, true), Parent(&Parent), Data(Content.data()),
        Size(Content.size()) {
    assert(isPowerOf2_64(Alignment) && "Alignment must be power of 2");
    assert(AlignmentOffset < Alignment &&
           "Alignment offset cannot exceed alignment");
    assert(AlignmentOffset <= MaxAlignmentOffset &&
           "Alignment offset exceeds maximum");
    ContentMutable = false;
    P2Align = Alignment ? llvm::countr_zero(Alignment) : 0;
    this->AlignmentOffset = AlignmentOffset;
  }

  /// Create a defined addressable for the given content.
  /// The content is assumed to be writable, and the caller is responsible
  /// for ensuring that it lives for the duration of the Block's lifetime.
  /// The standard way to achieve this is to allocate it on the Graph's
  /// allocator.
  Block(Section &Parent, MutableArrayRef<char> Content,
        orc::ExecutorAddr Address, uint64_t Alignment, uint64_t AlignmentOffset)
      : Addressable(Address, true), Parent(&Parent), Data(Content.data()),
        Size(Content.size()) {
    assert(isPowerOf2_64(Alignment) && "Alignment must be power of 2");
    assert(AlignmentOffset < Alignment &&
           "Alignment offset cannot exceed alignment");
    assert(AlignmentOffset <= MaxAlignmentOffset &&
           "Alignment offset exceeds maximum");
    ContentMutable = true;
    P2Align = Alignment ? llvm::countr_zero(Alignment) : 0;
    this->AlignmentOffset = AlignmentOffset;
  }

public:
  /// Vector of edges attached to this block.
  using EdgeVector = std::vector<Edge>;
  /// Iterator over edges attached to this block.
  using edge_iterator = EdgeVector::iterator;
  /// Const iterator over edges attached to this block.
  using const_edge_iterator = EdgeVector::const_iterator;

  /// Deleted copy constructor; Block is non-copyable.
  /// \param Other Unused; copy construction is deleted.
  Block(const Block &Other) = delete;
  /// Deleted copy assignment; Block is non-copyable.
  /// \param Other Unused; copy assignment is deleted.
  Block &operator=(const Block &Other) = delete;
  /// Deleted move constructor; Block is non-movable.
  /// \param Other Unused; move construction is deleted.
  Block(Block &&Other) = delete;
  /// Deleted move assignment; Block is non-movable.
  /// \param Other Unused; move assignment is deleted.
  Block &operator=(Block &&Other) = delete;

  /// Return the parent section for this block.
  /// \return The parent section for this block.
  Section &getSection() const { return *Parent; }

  /// Returns true if this is a zero-fill block.
  ///
  /// If true, getSize is callable but getContent is not (the content is
  /// defined to be a sequence of zero bytes of length Size).
  /// \return True if this is a zero-fill block.
  bool isZeroFill() const { return !Data; }

  /// Returns the size of this defined addressable.
  /// \return The size of this defined addressable.
  size_t getSize() const { return Size; }

  /// Turns this block into a zero-fill block of the given size.
  /// \param Size New zero-fill size in bytes.
  void setZeroFillSize(size_t Size) {
    Data = nullptr;
    this->Size = Size;
  }

  /// Returns the address range of this defined addressable.
  /// \return The address range of this defined addressable.
  orc::ExecutorAddrRange getRange() const {
    return orc::ExecutorAddrRange(getAddress(), getSize());
  }

  /// Get the content for this block. Block must not be a zero-fill block.
  /// \return The content for this block.
  ArrayRef<char> getContent() const {
    assert(Data && "Block does not contain content");
    return ArrayRef<char>(Data, Size);
  }

  /// Set the content for this block.
  /// Caller is responsible for ensuring the underlying bytes are not
  /// deallocated while pointed to by this block.
  /// \param Content Immutable content bytes for this block.
  void setContent(ArrayRef<char> Content) {
    assert(Content.data() && "Setting null content");
    Data = Content.data();
    Size = Content.size();
    ContentMutable = false;
  }

  /// Get mutable content for this block.
  ///
  /// If this Block's content is not already mutable this will trigger a copy
  /// of the existing immutable content to a new, mutable buffer allocated using
  /// LinkGraph::allocateContent.
  /// \param G Link graph whose allocator is used if content must be copied.
  /// \return Mutable content for this block.
  MutableArrayRef<char> getMutableContent(LinkGraph &G);

  /// Get mutable content for this block.
  ///
  /// This block's content must already be mutable. It is a programmatic error
  /// to call this on a block with immutable content -- consider using
  /// getMutableContent instead.
  /// \return Mutable content for this block.
  MutableArrayRef<char> getAlreadyMutableContent() {
    assert(Data && "Block does not contain content");
    assert(ContentMutable && "Content is not mutable");
    return MutableArrayRef<char>(const_cast<char *>(Data), Size);
  }

  /// Set mutable content for this block.
  ///
  /// The caller is responsible for ensuring that the memory pointed to by
  /// MutableContent is not deallocated while pointed to by this block.
  /// \param MutableContent Writable content buffer for this block.
  void setMutableContent(MutableArrayRef<char> MutableContent) {
    assert(MutableContent.data() && "Setting null content");
    Data = MutableContent.data();
    Size = MutableContent.size();
    ContentMutable = true;
  }

  /// Returns true if this block's content is mutable.
  ///
  /// This is primarily useful for asserting that a block is already in a
  /// mutable state prior to modifying the content. E.g. when applying
  /// fixups we expect the block to already be mutable as it should have been
  /// copied to working memory.
  /// \return True if this block's content is mutable.
  bool isContentMutable() const { return ContentMutable; }

  /// Get the alignment for this content.
  /// \return The alignment for this content.
  uint64_t getAlignment() const { return 1ull << P2Align; }

  /// Set the alignment for this content.
  /// \param Alignment Required alignment; must be a power of two.
  void setAlignment(uint64_t Alignment) {
    assert(isPowerOf2_64(Alignment) && "Alignment must be a power of two");
    P2Align = Alignment ? llvm::countr_zero(Alignment) : 0;
  }

  /// Get the alignment offset for this content.
  /// \return The alignment offset for this content.
  uint64_t getAlignmentOffset() const { return AlignmentOffset; }

  /// Set the alignment offset for this content.
  /// \param AlignmentOffset Offset applied when aligning this content.
  void setAlignmentOffset(uint64_t AlignmentOffset) {
    assert(AlignmentOffset < (1ull << P2Align) &&
           "Alignment offset can't exceed alignment");
    this->AlignmentOffset = AlignmentOffset;
  }

  /// Add an edge to this block.
  /// \param K Edge kind.
  /// \param Offset Offset within this block of the fixup.
  /// \param Target Symbol targeted by the edge.
  /// \param Addend Addend applied when fixing up.
  void addEdge(Edge::Kind K, Edge::OffsetT Offset, Symbol &Target,
               Edge::AddendT Addend) {
    assert((K == Edge::KeepAlive || !isZeroFill()) &&
           "Adding edge to zero-fill block?");
    Edges.push_back(Edge(K, Offset, Target, Addend));
  }

  /// Add an edge by copying an existing one. This is typically used when
  /// moving edges between blocks.
  /// \param E Existing edge to copy onto this block.
  void addEdge(const Edge &E) { Edges.push_back(E); }

  /// Return the list of edges attached to this content.
  /// \return The list of edges attached to this content.
  iterator_range<edge_iterator> edges() {
    return make_range(Edges.begin(), Edges.end());
  }

  /// Returns the list of edges attached to this content.
  /// \return The list of edges attached to this content.
  iterator_range<const_edge_iterator> edges() const {
    return make_range(Edges.begin(), Edges.end());
  }

  /// Returns an iterator over all edges at the given offset within the block.
  /// \param O Offset within the block whose edges are selected.
  /// \return An iterator over all edges at the given offset within the block.
  auto edges_at(Edge::OffsetT O) {
    return make_filter_range(edges(),
                             [O](const Edge &E) { return E.getOffset() == O; });
  }

  /// Returns an iterator over all edges at the given offset within the block.
  /// \param O Offset within the block whose edges are selected.
  /// \return An iterator over all edges at the given offset within the block.
  auto edges_at(Edge::OffsetT O) const {
    return make_filter_range(edges(),
                             [O](const Edge &E) { return E.getOffset() == O; });
  }

  /// Return the size of the edges list.
  /// \return The size of the edges list.
  size_t edges_size() const { return Edges.size(); }

  /// Returns true if the list of edges is empty.
  /// \return True if the list of edges is empty.
  bool edges_empty() const { return Edges.empty(); }

  /// Remove the edge pointed to by the given iterator.
  /// Returns an iterator to the new next element.
  /// \param I Iterator to the edge to remove.
  /// \return An iterator to the element following the erased edge.
  edge_iterator removeEdge(edge_iterator I) { return Edges.erase(I); }

  /// Returns the address of the fixup for the given edge, which is equal to
  /// this block's address plus the edge's offset.
  /// \param E Edge whose fixup address is requested.
  /// \return The fixup address for \p E within this block.
  orc::ExecutorAddr getFixupAddress(const Edge &E) const {
    return getAddress() + E.getOffset();
  }

private:
  static constexpr uint64_t MaxAlignmentOffset = (1ULL << 56) - 1;

  void setSection(Section &Parent) { this->Parent = &Parent; }

  Section *Parent;
  const char *Data = nullptr;
  size_t Size = 0;
  std::vector<Edge> Edges;
};

/// Align an address to conform with block alignment requirements.
/// \param Addr Address to align.
/// \param B Block whose alignment constraints are applied.
/// \return \p Addr adjusted to satisfy \p B's alignment.
inline uint64_t alignToBlock(uint64_t Addr, const Block &B) {
  uint64_t Delta = (B.getAlignmentOffset() - Addr) % B.getAlignment();
  return Addr + Delta;
}

/// Align an executor address to conform with block alignment requirements.
/// \param Addr Address to align.
/// \param B Block whose alignment constraints are applied.
/// \return \p Addr adjusted to satisfy \p B's alignment.
inline orc::ExecutorAddr alignToBlock(orc::ExecutorAddr Addr, const Block &B) {
  return orc::ExecutorAddr(alignToBlock(Addr.getValue(), B));
}

/// Return whether the given block contains exactly one valid C-string.
///
/// Zero-fill blocks of size 1 count as valid empty strings. Content blocks
/// must end with a zero, and contain no zeros before the end.
/// \param B Block to inspect.
/// \return True if \p B contains exactly one valid C-string.
LLVM_ABI bool isCStringBlock(Block &B);

/// Describes symbol linkage. This can be used to resolve definition clashes.
enum class Linkage : uint8_t {
  Strong, ///< Strong definition; clashes are errors.
  Weak,   ///< Weak definition; may be overridden.
};

/// Holds target-specific properties for a symbol.
using TargetFlagsType = uint8_t;

/// For errors and debugging output.
/// \param L Linkage value to name.
/// \return A human-readable name for \p L.
LLVM_ABI const char *getLinkageName(Linkage L);

/// Defines the scope in which this symbol should be visible.
///
///   Default -- Visible in the public interface of the linkage unit.
///   Hidden -- Visible within the linkage unit, but not exported from it.
///   SideEffectsOnly -- Like hidden, but symbol can only be looked up once
///                      to trigger materialization of the containing graph.
///   Local -- Visible only within the LinkGraph.
enum class Scope : uint8_t {
  Default,         ///< Visible in the public interface of the linkage unit.
  Hidden,          ///< Visible within the linkage unit only.
  SideEffectsOnly, ///< Hidden; lookup only triggers materialization once.
  Local,           ///< Visible only within the LinkGraph.
};

/// For debugging output.
/// \param S Scope value to name.
/// \return A human-readable name for \p S.
LLVM_ABI const char *getScopeName(Scope S);

/// Write a description of block \p B to \p OS.
/// \param OS Output stream to write to.
/// \param B Block to print.
/// \return A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Block &B);

/// Symbol representation.
///
/// Symbols represent locations within Addressable objects.
/// They can be either Named or Anonymous.
/// Anonymous symbols have neither linkage nor visibility, and must point at
/// ContentBlocks.
/// Named symbols may be in one of four states:
///   - Null: Default initialized. Assignable, but otherwise unusable.
///   - Defined: Has both linkage and visibility and points to a ContentBlock
///   - Common: Has both linkage and visibility, points to a null Addressable.
///   - External: Has neither linkage nor visibility, points to an external
///     Addressable.
///
class Symbol {
  friend class LinkGraph;

private:
  Symbol(Addressable &Base, orc::ExecutorAddrDiff Offset,
         orc::SymbolStringPtr &&Name, orc::ExecutorAddrDiff Size, Linkage L,
         Scope S, bool IsLive, bool IsCallable)
      : Name(std::move(Name)), Base(&Base), Offset(Offset), WeakRef(0),
        Size(Size) {
    assert(Offset <= MaxOffset && "Offset out of range");
    setLinkage(L);
    setScope(S);
    setLive(IsLive);
    setCallable(IsCallable);
    setTargetFlags(TargetFlagsType{});
  }

  static Symbol &constructExternal(BumpPtrAllocator &Allocator,
                                   Addressable &Base,
                                   orc::SymbolStringPtr &&Name,
                                   orc::ExecutorAddrDiff Size, Linkage L,
                                   bool WeaklyReferenced) {
    assert(!Base.isDefined() &&
           "Cannot create external symbol from defined block");
    assert(Name && "External symbol name cannot be empty");
    auto *Sym = Allocator.Allocate<Symbol>();
    new (Sym)
        Symbol(Base, 0, std::move(Name), Size, L, Scope::Default, false, false);
    Sym->setWeaklyReferenced(WeaklyReferenced);
    return *Sym;
  }

  static Symbol &constructAbsolute(BumpPtrAllocator &Allocator,
                                   Addressable &Base,
                                   orc::SymbolStringPtr &&Name,
                                   orc::ExecutorAddrDiff Size, Linkage L,
                                   Scope S, bool IsLive) {
    assert(!Base.isDefined() &&
           "Cannot create absolute symbol from a defined block");
    auto *Sym = Allocator.Allocate<Symbol>();
    new (Sym) Symbol(Base, 0, std::move(Name), Size, L, S, IsLive, false);
    return *Sym;
  }

  static Symbol &constructAnonDef(BumpPtrAllocator &Allocator, Block &Base,
                                  orc::ExecutorAddrDiff Offset,
                                  orc::ExecutorAddrDiff Size, bool IsCallable,
                                  bool IsLive) {
    assert((Offset + Size) <= Base.getSize() &&
           "Symbol extends past end of block");
    auto *Sym = Allocator.Allocate<Symbol>();
    new (Sym) Symbol(Base, Offset, nullptr, Size, Linkage::Strong, Scope::Local,
                     IsLive, IsCallable);
    return *Sym;
  }

  static Symbol &constructNamedDef(BumpPtrAllocator &Allocator, Block &Base,
                                   orc::ExecutorAddrDiff Offset,
                                   orc::SymbolStringPtr Name,
                                   orc::ExecutorAddrDiff Size, Linkage L,
                                   Scope S, bool IsLive, bool IsCallable) {
    assert((Offset + Size) <= Base.getSize() &&
           "Symbol extends past end of block");
    assert(Name && "Name cannot be empty");
    auto *Sym = Allocator.Allocate<Symbol>();
    new (Sym)
        Symbol(Base, Offset, std::move(Name), Size, L, S, IsLive, IsCallable);
    return *Sym;
  }

public:
  /// Create a null Symbol. This allows Symbols to be default initialized for
  /// use in containers (e.g. as map values). Null symbols are only useful for
  /// assigning to.
  Symbol() = default;

  // Symbols are not movable or copyable.
  /// Deleted copy constructor; Symbol is non-copyable.
  /// \param Other Unused; copy construction is deleted.
  Symbol(const Symbol &Other) = delete;
  /// Deleted copy assignment; Symbol is non-copyable.
  /// \param Other Unused; copy assignment is deleted.
  Symbol &operator=(const Symbol &Other) = delete;
  /// Deleted move constructor; Symbol is non-movable.
  /// \param Other Unused; move construction is deleted.
  Symbol(Symbol &&Other) = delete;
  /// Deleted move assignment; Symbol is non-movable.
  /// \param Other Unused; move assignment is deleted.
  Symbol &operator=(Symbol &&Other) = delete;

  /// Returns true if this symbol has a name.
  /// \return True if this symbol has a name.
  bool hasName() const { return Name != nullptr; }

  /// Returns the name of this symbol (empty if the symbol is anonymous).
  /// \return The name of this symbol (empty if the symbol is anonymous).
  const orc::SymbolStringPtr &getName() const {
    assert((hasName() || getScope() == Scope::Local) &&
           "Anonymous symbol has non-local scope");

    return Name;
  }

  /// Rename this symbol. The client is responsible for updating scope and
  /// linkage if this name-change requires it.
  /// \param Name New interned name for this symbol.
  void setName(orc::SymbolStringPtr Name) { this->Name = std::move(Name); }

  /// Returns true if this Symbol has content (potentially) defined within this
  /// object file (i.e. is anything but an external or absolute symbol).
  /// \return True if this symbol is defined in this object file.
  bool isDefined() const {
    assert(Base && "Attempt to access null symbol");
    return Base->isDefined();
  }

  /// Returns true if this symbol is live (i.e. should be treated as a root for
  /// dead stripping).
  /// \return True if this symbol should be treated as live for dead stripping.
  bool isLive() const {
    assert(Base && "Attempting to access null symbol");
    return IsLive;
  }

  /// Set this symbol's live bit.
  /// \param IsLive Whether the symbol should be treated as live.
  void setLive(bool IsLive) { this->IsLive = IsLive; }

  /// Returns true is this symbol is callable.
  /// \return True if this symbol is callable.
  bool isCallable() const { return IsCallable; }

  /// Set this symbol's callable bit.
  /// \param IsCallable Whether the symbol is callable.
  void setCallable(bool IsCallable) { this->IsCallable = IsCallable; }

  /// Returns true if the underlying addressable is an unresolved external.
  /// \return True if the underlying addressable is an unresolved external.
  bool isExternal() const {
    assert(Base && "Attempt to access null symbol");
    return !Base->isDefined() && !Base->isAbsolute();
  }

  /// Returns true if the underlying addressable is an absolute symbol.
  /// \return True if the underlying addressable is an absolute symbol.
  bool isAbsolute() const {
    assert(Base && "Attempt to access null symbol");
    return Base->isAbsolute();
  }

  /// Return the addressable that this symbol points to.
  /// \return The addressable that this symbol points to.
  Addressable &getAddressable() {
    assert(Base && "Cannot get underlying addressable for null symbol");
    return *Base;
  }

  /// Return the addressable that this symbol points to.
  /// \return The addressable that this symbol points to.
  const Addressable &getAddressable() const {
    assert(Base && "Cannot get underlying addressable for null symbol");
    return *Base;
  }

  /// Return the Block for this Symbol (Symbol must be defined).
  /// \return The Block for this Symbol (Symbol must be defined).
  Block &getBlock() {
    assert(Base && "Cannot get block for null symbol");
    assert(Base->isDefined() && "Not a defined symbol");
    return static_cast<Block &>(*Base);
  }

  /// Return the Block for this Symbol (Symbol must be defined).
  /// \return The Block for this Symbol (Symbol must be defined).
  const Block &getBlock() const {
    assert(Base && "Cannot get block for null symbol");
    assert(Base->isDefined() && "Not a defined symbol");
    return static_cast<const Block &>(*Base);
  }

  /// Return the Section for this Symbol (Symbol must be defined).
  /// \return The Section for this Symbol (Symbol must be defined).
  Section &getSection() const { return getBlock().getSection(); }

  /// Returns the offset for this symbol within the underlying addressable.
  /// \return The offset for this symbol within the underlying addressable.
  orc::ExecutorAddrDiff getOffset() const { return Offset; }

  /// Set the offset of this symbol within its underlying addressable.
  /// \param NewOffset New offset within the underlying addressable.
  void setOffset(orc::ExecutorAddrDiff NewOffset) {
    assert(NewOffset <= getBlock().getSize() && "Offset out of range");
    Offset = NewOffset;
  }

  /// Returns the address of this symbol.
  /// \return The address of this symbol.
  orc::ExecutorAddr getAddress() const { return Base->getAddress() + Offset; }

  /// Returns the size of this symbol.
  /// \return The size of this symbol.
  orc::ExecutorAddrDiff getSize() const { return Size; }

  /// Set the size of this symbol.
  /// \param Size New size for this symbol.
  void setSize(orc::ExecutorAddrDiff Size) {
    assert(Base && "Cannot set size for null Symbol");
    assert((Size == 0 || Base->isDefined()) &&
           "Non-zero size can only be set for defined symbols");
    assert((Offset + Size <= static_cast<const Block &>(*Base).getSize()) &&
           "Symbol size cannot extend past the end of its containing block");
    this->Size = Size;
  }

  /// Returns the address range of this symbol.
  /// \return The address range of this symbol.
  orc::ExecutorAddrRange getRange() const {
    return orc::ExecutorAddrRange(getAddress(), getSize());
  }

  /// Returns true if this symbol is backed by a zero-fill block.
  /// This method may only be called on defined symbols.
  /// \return True if this symbol is backed by a zero-fill block.
  bool isSymbolZeroFill() const { return getBlock().isZeroFill(); }

  /// Returns the content in the underlying block covered by this symbol.
  /// This method may only be called on defined non-zero-fill symbols.
  /// \return The content in the underlying block covered by this symbol.
  ArrayRef<char> getSymbolContent() const {
    return getBlock().getContent().slice(Offset, Size);
  }

  /// Get the linkage for this Symbol.
  /// \return The linkage for this Symbol.
  Linkage getLinkage() const { return static_cast<Linkage>(L); }

  /// Set the linkage for this Symbol.
  /// \param L New linkage value.
  void setLinkage(Linkage L) {
    assert((L == Linkage::Strong || (!Base->isAbsolute() && Name)) &&
           "Linkage can only be applied to defined named symbols");
    this->L = static_cast<uint8_t>(L);
  }

  /// Get the visibility for this Symbol.
  /// \return The visibility for this Symbol.
  Scope getScope() const { return static_cast<Scope>(S); }

  /// Set the visibility for this Symbol.
  /// \param S New scope/visibility value.
  void setScope(Scope S) {
    assert((hasName() || S == Scope::Local) &&
           "Can not set anonymous symbol to non-local scope");
    assert((S != Scope::Local || Base->isDefined() || Base->isAbsolute()) &&
           "Invalid visibility for symbol type");
    this->S = static_cast<uint8_t>(S);
  }

  /// Get the target flags of this Symbol.
  /// \return The target flags of this Symbol.
  TargetFlagsType getTargetFlags() const { return TargetFlags; }

  /// Set the target flags for this Symbol.
  /// \param Flags New target-specific flags.
  void setTargetFlags(TargetFlagsType Flags) {
    assert(Flags <= 1 && "Add more bits to store more than single flag");
    TargetFlags = Flags;
  }

  /// Returns true if this is a weakly referenced external symbol.
  /// This method may only be called on external symbols.
  /// \return True if this is a weakly referenced external symbol.
  bool isWeaklyReferenced() const {
    assert(isExternal() && "isWeaklyReferenced called on non-external");
    return WeakRef;
  }

  /// Set the WeaklyReferenced value for this symbol.
  /// This method may only be called on external symbols.
  /// \param WeakRef Whether the external may remain unresolved.
  void setWeaklyReferenced(bool WeakRef) {
    assert(isExternal() && "setWeaklyReferenced called on non-external");
    this->WeakRef = WeakRef;
  }

private:
  void makeExternal(Addressable &A) {
    assert(!A.isDefined() && !A.isAbsolute() &&
           "Attempting to make external with defined or absolute block");
    Base = &A;
    Offset = 0;
    setScope(Scope::Default);
    IsLive = 0;
    // note: Size, Linkage and IsCallable fields left unchanged.
  }

  void makeAbsolute(Addressable &A) {
    assert(!A.isDefined() && A.isAbsolute() &&
           "Attempting to make absolute with defined or external block");
    Base = &A;
    Offset = 0;
  }

  void setBlock(Block &B) { Base = &B; }

  static constexpr uint64_t MaxOffset = (1ULL << 59) - 1;

  orc::SymbolStringPtr Name = nullptr;
  Addressable *Base = nullptr;
  uint64_t Offset : 57;
  uint64_t L : 1;
  uint64_t S : 2;
  uint64_t IsLive : 1;
  uint64_t IsCallable : 1;
  uint64_t WeakRef : 1;
  uint64_t TargetFlags : 1;
  size_t Size = 0;
};

/// Write a description of symbol \p A to \p OS.
/// \param OS Output stream to write to.
/// \param A Symbol to print.
/// \return A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Symbol &A);

/// Print a description of edge \p E in block \p B to \p OS.
/// \param OS Output stream to write to.
/// \param B Block that owns the edge.
/// \param E Edge to print.
/// \param EdgeKindName Human-readable name for the edge kind.
LLVM_ABI void printEdge(raw_ostream &OS, const Block &B, const Edge &E,
                        StringRef EdgeKindName);

/// Represents an object file section.
class Section {
  friend class LinkGraph;

private:
  Section(StringRef Name, orc::MemProt Prot, SectionOrdinal SecOrdinal)
      : Name(Name), Prot(Prot), SecOrdinal(SecOrdinal) {}

  using SymbolSet = DenseSet<Symbol *>;
  using BlockSet = DenseSet<Block *>;

public:
  /// Iterator over symbols defined in this section.
  using symbol_iterator = SymbolSet::iterator;
  /// Const iterator over symbols defined in this section.
  using const_symbol_iterator = SymbolSet::const_iterator;

  /// Iterator over blocks defined in this section.
  using block_iterator = BlockSet::iterator;
  /// Const iterator over blocks defined in this section.
  using const_block_iterator = BlockSet::const_iterator;

  /// Destroy this section.
  LLVM_ABI ~Section();

  // Sections are not movable or copyable.
  /// Deleted copy constructor; Section is non-copyable.
  /// \param Other Unused; copy construction is deleted.
  Section(const Section &Other) = delete;
  /// Deleted copy assignment; Section is non-copyable.
  /// \param Other Unused; copy assignment is deleted.
  Section &operator=(const Section &Other) = delete;
  /// Deleted move constructor; Section is non-movable.
  /// \param Other Unused; move construction is deleted.
  Section(Section &&Other) = delete;
  /// Deleted move assignment; Section is non-movable.
  /// \param Other Unused; move assignment is deleted.
  Section &operator=(Section &&Other) = delete;

  /// Returns the name of this section.
  /// \return The name of this section.
  StringRef getName() const { return Name; }

  /// Returns the protection flags for this section.
  /// \return The protection flags for this section.
  orc::MemProt getMemProt() const { return Prot; }

  /// Set the protection flags for this section.
  /// \param Prot New memory protection flags.
  void setMemProt(orc::MemProt Prot) { this->Prot = Prot; }

  /// Get the memory lifetime policy for this section.
  /// \return The memory lifetime policy for this section.
  orc::MemLifetime getMemLifetime() const { return ML; }

  /// Set the memory lifetime policy for this section.
  /// \param ML New memory lifetime policy.
  void setMemLifetime(orc::MemLifetime ML) { this->ML = ML; }

  /// Returns the ordinal for this section.
  /// \return The ordinal for this section.
  SectionOrdinal getOrdinal() const { return SecOrdinal; }

  /// Set the ordinal for this section. Ordinals are used to order the layout
  /// of sections with the same permissions.
  /// \param SecOrdinal New section ordinal for layout ordering.
  void setOrdinal(SectionOrdinal SecOrdinal) { this->SecOrdinal = SecOrdinal; }

  /// Returns true if this section is empty (contains no blocks or symbols).
  /// \return True if this section is empty (contains no blocks or symbols).
  bool empty() const { return Blocks.empty(); }

  /// Returns an iterator over the blocks defined in this section.
  /// \return An iterator over the blocks defined in this section.
  iterator_range<block_iterator> blocks() {
    return make_range(Blocks.begin(), Blocks.end());
  }

  /// Returns an iterator over the blocks defined in this section.
  /// \return An iterator over the blocks defined in this section.
  iterator_range<const_block_iterator> blocks() const {
    return make_range(Blocks.begin(), Blocks.end());
  }

  /// Returns the number of blocks in this section.
  /// \return The number of blocks in this section.
  BlockSet::size_type blocks_size() const { return Blocks.size(); }

  /// Returns an iterator over the symbols defined in this section.
  /// \return An iterator over the symbols defined in this section.
  iterator_range<symbol_iterator> symbols() {
    return make_range(Symbols.begin(), Symbols.end());
  }

  /// Returns an iterator over the symbols defined in this section.
  /// \return An iterator over the symbols defined in this section.
  iterator_range<const_symbol_iterator> symbols() const {
    return make_range(Symbols.begin(), Symbols.end());
  }

  /// Return the number of symbols in this section.
  /// \return The number of symbols in this section.
  SymbolSet::size_type symbols_size() const { return Symbols.size(); }

private:
  void addSymbol(Symbol &Sym) {
    assert(!Symbols.count(&Sym) && "Symbol is already in this section");
    Symbols.insert(&Sym);
  }

  void removeSymbol(Symbol &Sym) {
    assert(Symbols.count(&Sym) && "symbol is not in this section");
    Symbols.erase(&Sym);
  }

  void addBlock(Block &B) {
    assert(!Blocks.count(&B) && "Block is already in this section");
    Blocks.insert(&B);
  }

  void removeBlock(Block &B) {
    assert(Blocks.count(&B) && "Block is not in this section");
    Blocks.erase(&B);
  }

  void transferContentTo(Section &DstSection) {
    if (&DstSection == this)
      return;
    for (auto *S : Symbols)
      DstSection.addSymbol(*S);
    for (auto *B : Blocks)
      DstSection.addBlock(*B);
    Symbols.clear();
    Blocks.clear();
  }

  StringRef Name;
  orc::MemProt Prot;
  orc::MemLifetime ML = orc::MemLifetime::Standard;
  SectionOrdinal SecOrdinal = 0;
  BlockSet Blocks;
  SymbolSet Symbols;
};

/// Represents a section address range via a pair of Block pointers
/// to the first and last Blocks in the section.
class SectionRange {
public:
  /// Create an empty section range.
  SectionRange() = default;
  /// Build a range covering the blocks of \p Sec.
  /// \param Sec Section whose blocks define the address range.
  SectionRange(const Section &Sec) {
    if (Sec.blocks().empty())
      return;
    First = Last = *Sec.blocks().begin();
    for (auto *B : Sec.blocks()) {
      if (B->getAddress() < First->getAddress())
        First = B;
      if (B->getAddress() > Last->getAddress())
        Last = B;
    }
  }
  /// Return the first block in the range, or null if empty.
  /// \return The first block in the range, or null if empty.
  Block *getFirstBlock() const {
    assert((!Last || First) && "First can not be null if end is non-null");
    return First;
  }
  /// Return the last block in the range, or null if empty.
  /// \return The last block in the range, or null if empty.
  Block *getLastBlock() const {
    assert((First || !Last) && "Last can not be null if start is non-null");
    return Last;
  }
  /// Return whether the range contains no blocks.
  /// \return True if the range contains no blocks.
  bool empty() const {
    assert((First || !Last) && "Last can not be null if start is non-null");
    return !First;
  }
  /// Return the start address of the range.
  /// \return The start address of the range.
  orc::ExecutorAddr getStart() const {
    return First ? First->getAddress() : orc::ExecutorAddr();
  }
  /// Return the end address (one past the last byte) of the range.
  /// \return The end address (one past the last byte) of the range.
  orc::ExecutorAddr getEnd() const {
    return Last ? Last->getAddress() + Last->getSize() : orc::ExecutorAddr();
  }
  /// Return the size in bytes of the range.
  /// \return The size in bytes of the range.
  orc::ExecutorAddrDiff getSize() const { return getEnd() - getStart(); }

  /// Return the half-open address range covered by this section range.
  /// \return The half-open address range covered by this section range.
  orc::ExecutorAddrRange getRange() const {
    return orc::ExecutorAddrRange(getStart(), getEnd());
  }

private:
  Block *First = nullptr;
  Block *Last = nullptr;
};

/// Graph of sections, blocks, symbols, and edges for one link unit.
class LinkGraph {
private:
  using SectionMap = DenseMap<StringRef, std::unique_ptr<Section>>;
  using ExternalSymbolMap = DenseMap<orc::NonOwningSymbolStringPtr, Symbol *>;
  using AbsoluteSymbolSet = DenseSet<Symbol *>;
  using BlockSet = DenseSet<Block *>;

  template <typename... ArgTs>
  Addressable &createAddressable(ArgTs &&... Args) {
    Addressable *A =
        reinterpret_cast<Addressable *>(Allocator.Allocate<Addressable>());
    new (A) Addressable(std::forward<ArgTs>(Args)...);
    return *A;
  }

  void destroyAddressable(Addressable &A) {
    A.~Addressable();
    Allocator.Deallocate(&A);
  }

  template <typename... ArgTs> Block &createBlock(ArgTs &&... Args) {
    Block *B = reinterpret_cast<Block *>(Allocator.Allocate<Block>());
    new (B) Block(std::forward<ArgTs>(Args)...);
    B->getSection().addBlock(*B);
    return *B;
  }

  void destroyBlock(Block &B) {
    B.~Block();
    Allocator.Deallocate(&B);
  }

  void destroySymbol(Symbol &S) {
    S.~Symbol();
    Allocator.Deallocate(&S);
  }

  static iterator_range<Section::block_iterator> getSectionBlocks(Section &S) {
    return S.blocks();
  }

  static iterator_range<Section::const_block_iterator>
  getSectionConstBlocks(const Section &S) {
    return S.blocks();
  }

  static iterator_range<Section::symbol_iterator>
  getSectionSymbols(Section &S) {
    return S.symbols();
  }

  static iterator_range<Section::const_symbol_iterator>
  getSectionConstSymbols(const Section &S) {
    return S.symbols();
  }

  struct GetExternalSymbolMapEntryValue {
    Symbol *operator()(ExternalSymbolMap::value_type &KV) const {
      return KV.second;
    }
  };

  struct GetSectionMapEntryValue {
    Section &operator()(SectionMap::value_type &KV) const { return *KV.second; }
  };

  struct GetSectionMapEntryConstValue {
    const Section &operator()(const SectionMap::value_type &KV) const {
      return *KV.second;
    }
  };

public:
  /// Iterator over external symbols in the graph.
  using external_symbol_iterator =
      mapped_iterator<ExternalSymbolMap::iterator,
                      GetExternalSymbolMapEntryValue>;
  /// Iterator over absolute symbols in the graph.
  using absolute_symbol_iterator = AbsoluteSymbolSet::iterator;

  /// Iterator over sections in the graph.
  using section_iterator =
      mapped_iterator<SectionMap::iterator, GetSectionMapEntryValue>;
  /// Const iterator over sections in the graph.
  using const_section_iterator =
      mapped_iterator<SectionMap::const_iterator, GetSectionMapEntryConstValue>;

  /// Forward iterator that flattens nested outer/inner collections.
  template <typename OuterItrT, typename InnerItrT, typename T,
            iterator_range<InnerItrT> getInnerRange(
                typename OuterItrT::reference)>
  class nested_collection_iterator
      : public iterator_facade_base<
            nested_collection_iterator<OuterItrT, InnerItrT, T, getInnerRange>,
            std::forward_iterator_tag, T> {
  public:
    /// Create an empty (end) nested collection iterator.
    nested_collection_iterator() = default;

    /// Create an iterator covering the given outer range.
    /// \param OuterI Beginning of the outer iterator range.
    /// \param OuterE End of the outer iterator range.
    nested_collection_iterator(OuterItrT OuterI, OuterItrT OuterE)
        : OuterI(OuterI), OuterE(OuterE),
          InnerI(getInnerBegin(OuterI, OuterE)) {
      moveToNonEmptyInnerOrEnd();
    }

    /// Return whether this iterator equals \p RHS.
    /// \param RHS Iterator to compare against.
    /// \return True if this iterator equals \p RHS.
    bool operator==(const nested_collection_iterator &RHS) const {
      return (OuterI == RHS.OuterI) && (InnerI == RHS.InnerI);
    }

    /// Return the current inner element.
    /// \return The current inner element.
    T operator*() const {
      assert(InnerI != getInnerRange(*OuterI).end() && "Dereferencing end?");
      return *InnerI;
    }

    /// Advance to the next element in the flattened sequence.
    /// \return A reference to this iterator after advancing.
    nested_collection_iterator operator++() {
      ++InnerI;
      moveToNonEmptyInnerOrEnd();
      return *this;
    }

  private:
    static InnerItrT getInnerBegin(OuterItrT OuterI, OuterItrT OuterE) {
      return OuterI != OuterE ? getInnerRange(*OuterI).begin() : InnerItrT();
    }

    void moveToNonEmptyInnerOrEnd() {
      while (OuterI != OuterE && InnerI == getInnerRange(*OuterI).end()) {
        ++OuterI;
        InnerI = getInnerBegin(OuterI, OuterE);
      }
    }

    OuterItrT OuterI, OuterE;
    InnerItrT InnerI;
  };

  /// Iterator over defined symbols across all sections.
  using defined_symbol_iterator =
      nested_collection_iterator<section_iterator, Section::symbol_iterator,
                                 Symbol *, getSectionSymbols>;

  /// Const iterator over defined symbols across all sections.
  using const_defined_symbol_iterator =
      nested_collection_iterator<const_section_iterator,
                                 Section::const_symbol_iterator, const Symbol *,
                                 getSectionConstSymbols>;

  /// Iterator over blocks across all sections.
  using block_iterator =
      nested_collection_iterator<section_iterator, Section::block_iterator,
                                 Block *, getSectionBlocks>;

  /// Const iterator over blocks across all sections.
  using const_block_iterator =
      nested_collection_iterator<const_section_iterator,
                                 Section::const_block_iterator, const Block *,
                                 getSectionConstBlocks>;

  /// Function type that maps an edge kind to a printable name.
  using GetEdgeKindNameFunction = const char *(*)(Edge::Kind);

  /// Construct a link graph for the given target.
  /// \param Name Graph name, usually the original buffer name.
  /// \param SSP Symbol string pool used to intern names.
  /// \param TT Target triple for content in this graph.
  /// \param Features Subtarget features for this graph.
  /// \param GetEdgeKindName Callback that names architecture-specific edges.
  LinkGraph(std::string Name, std::shared_ptr<orc::SymbolStringPool> SSP,
            Triple TT, SubtargetFeatures Features,
            GetEdgeKindNameFunction GetEdgeKindName)
      : Name(std::move(Name)), SSP(std::move(SSP)), TT(std::move(TT)),
        Features(std::move(Features)),
        GetEdgeKindName(std::move(GetEdgeKindName)) {
    assert(!(Triple::getArchPointerBitWidth(this->TT.getArch()) % 8) &&
           "Arch bitwidth is not a multiple of 8");
  }

  /// Deleted copy constructor; LinkGraph is non-copyable.
  /// \param Other Unused; copy construction is deleted.
  LinkGraph(const LinkGraph &Other) = delete;
  /// Deleted copy assignment; LinkGraph is non-copyable.
  /// \param Other Unused; copy assignment is deleted.
  LinkGraph &operator=(const LinkGraph &Other) = delete;
  /// Deleted move constructor; LinkGraph is non-movable.
  /// \param Other Unused; move construction is deleted.
  LinkGraph(LinkGraph &&Other) = delete;
  /// Deleted move assignment; LinkGraph is non-movable.
  /// \param Other Unused; move assignment is deleted.
  LinkGraph &operator=(LinkGraph &&Other) = delete;
  /// Destroy the link graph and its owned nodes.
  LLVM_ABI ~LinkGraph();

  /// Returns the name of this graph (usually the name of the original
  /// underlying MemoryBuffer).
  /// \return The name of this graph.
  const std::string &getName() const { return Name; }

  /// Returns the target triple for this Graph.
  /// \return The target triple for this Graph.
  const Triple &getTargetTriple() const { return TT; }

  /// Return the subtarget features for this Graph.
  /// \return The subtarget features for this Graph.
  const SubtargetFeatures &getFeatures() const { return Features; }

  /// Returns the pointer size for use in this graph.
  /// \return The pointer size for use in this graph.
  unsigned getPointerSize() const { return TT.getArchPointerBitWidth() / 8; }

  /// Returns the endianness of content in this graph.
  /// \return The endianness of content in this graph.
  llvm::endianness getEndianness() const {
    return TT.isLittleEndian() ? endianness::little : endianness::big;
  }

  /// Return the human-readable name for the given edge kind.
  /// \param K Edge kind to name.
  /// \return The human-readable name for the given edge kind.
  const char *getEdgeKindName(Edge::Kind K) const { return GetEdgeKindName(K); }

  /// Return the symbol string pool used by this graph.
  /// \return The symbol string pool used by this graph.
  std::shared_ptr<orc::SymbolStringPool> getSymbolStringPool() { return SSP; }

  /// Allocate a mutable buffer of the given size using the LinkGraph's
  /// allocator.
  /// \param Size Number of bytes to allocate.
  /// \return A mutable buffer of the requested size.
  MutableArrayRef<char> allocateBuffer(size_t Size) {
    return {Allocator.Allocate<char>(Size), Size};
  }

  /// Allocate a copy of the given string using the LinkGraph's allocator.
  /// This can be useful when renaming symbols or adding new content to the
  /// graph.
  /// \param Source Bytes to copy into graph-owned storage.
  /// \return A mutable copy of the source in graph-owned storage.
  MutableArrayRef<char> allocateContent(ArrayRef<char> Source) {
    auto *AllocatedBuffer = Allocator.Allocate<char>(Source.size());
    llvm::copy(Source, AllocatedBuffer);
    return MutableArrayRef<char>(AllocatedBuffer, Source.size());
  }

  /// Allocate a copy of the given string using the LinkGraph's allocator.
  /// This can be useful when renaming symbols or adding new content to the
  /// graph.
  ///
  /// Note: This Twine-based overload requires an extra string copy and an
  /// extra heap allocation for large strings. The ArrayRef<char> overload
  /// should be preferred where possible.
  /// \param Source Twine to copy into graph-owned storage.
  /// \return A mutable copy of the source in graph-owned storage.
  MutableArrayRef<char> allocateContent(Twine Source) {
    SmallString<256> TmpBuffer;
    auto SourceStr = Source.toStringRef(TmpBuffer);
    auto *AllocatedBuffer = Allocator.Allocate<char>(SourceStr.size());
    llvm::copy(SourceStr, AllocatedBuffer);
    return MutableArrayRef<char>(AllocatedBuffer, SourceStr.size());
  }

  /// Allocate a copy of the given string using the LinkGraph's allocator
  /// and return it as a StringRef.
  ///
  /// This is a convenience wrapper around allocateContent(Twine) that is
  /// handy when creating new symbol names within the graph.
  /// \param Source Twine to copy and return as a StringRef name.
  /// \return The allocated name as a StringRef into graph-owned storage.
  StringRef allocateName(Twine Source) {
    auto Buf = allocateContent(Source);
    return {Buf.data(), Buf.size()};
  }

  /// Allocate a copy of the given string using the LinkGraph's allocator.
  ///
  /// The allocated string will be terminated with a null character, and the
  /// returned MutableArrayRef will include this null character in the last
  /// position.
  /// \param Source String to copy into a null-terminated buffer.
  /// \return A null-terminated mutable copy of the source in graph-owned storage.
  MutableArrayRef<char> allocateCString(StringRef Source) {
    char *AllocatedBuffer = Allocator.Allocate<char>(Source.size() + 1);
    llvm::copy(Source, AllocatedBuffer);
    AllocatedBuffer[Source.size()] = '\0';
    return MutableArrayRef<char>(AllocatedBuffer, Source.size() + 1);
  }

  /// Allocate a copy of the given string using the LinkGraph's allocator.
  ///
  /// The allocated string will be terminated with a null character, and the
  /// returned MutableArrayRef will include this null character in the last
  /// position.
  ///
  /// Note: This Twine-based overload requires an extra string copy and an
  /// extra heap allocation for large strings. The ArrayRef<char> overload
  /// should be preferred where possible.
  /// \param Source Twine to copy into a null-terminated buffer.
  /// \return A null-terminated mutable copy of the source in graph-owned storage.
  MutableArrayRef<char> allocateCString(Twine Source) {
    SmallString<256> TmpBuffer;
    auto SourceStr = Source.toStringRef(TmpBuffer);
    auto *AllocatedBuffer = Allocator.Allocate<char>(SourceStr.size() + 1);
    llvm::copy(SourceStr, AllocatedBuffer);
    AllocatedBuffer[SourceStr.size()] = '\0';
    return MutableArrayRef<char>(AllocatedBuffer, SourceStr.size() + 1);
  }

  /// Create a section with the given name, protection flags.
  /// \param Name Section name; must be unique in this graph.
  /// \param Prot Memory protection flags for the section.
  /// \return A reference to the newly created section.
  Section &createSection(StringRef Name, orc::MemProt Prot) {
    assert(!Sections.count(Name) && "Duplicate section name");
    std::unique_ptr<Section> Sec(new Section(Name, Prot, Sections.size()));
    return *Sections.insert(std::make_pair(Name, std::move(Sec))).first->second;
  }

  /// Create a content block.
  /// \param Parent Section that will own the new block.
  /// \param Content Immutable content bytes for the block.
  /// \param Address Address assigned to the block.
  /// \param Alignment Required alignment of the block.
  /// \param AlignmentOffset Offset applied when aligning the block.
  /// \return A reference to the newly created content block.
  Block &createContentBlock(Section &Parent, ArrayRef<char> Content,
                            orc::ExecutorAddr Address, uint64_t Alignment,
                            uint64_t AlignmentOffset) {
    return createBlock(Parent, Content, Address, Alignment, AlignmentOffset);
  }

  /// Create a content block with initially mutable data.
  /// \param Parent Section that will own the new block.
  /// \param MutableContent Writable content buffer for the block.
  /// \param Address Address assigned to the block.
  /// \param Alignment Required alignment of the block.
  /// \param AlignmentOffset Offset applied when aligning the block.
  /// \return A reference to the newly created mutable content block.
  Block &createMutableContentBlock(Section &Parent,
                                   MutableArrayRef<char> MutableContent,
                                   orc::ExecutorAddr Address,
                                   uint64_t Alignment,
                                   uint64_t AlignmentOffset) {
    return createBlock(Parent, MutableContent, Address, Alignment,
                       AlignmentOffset);
  }

  /// Create a mutable content block of the given size.
  ///
  /// Content will be allocated via the LinkGraph's allocateBuffer method.
  /// By default the memory will be zero-initialized. Passing false for
  /// ZeroInitialize will prevent this.
  /// \param Parent Section that will own the new block.
  /// \param ContentSize Number of bytes to allocate for the block.
  /// \param Address Address assigned to the block.
  /// \param Alignment Required alignment of the block.
  /// \param AlignmentOffset Offset applied when aligning the block.
  /// \param ZeroInitialize Whether to zero-fill the allocated content.
  /// \return A reference to the newly created mutable content block.
  Block &createMutableContentBlock(Section &Parent, size_t ContentSize,
                                   orc::ExecutorAddr Address,
                                   uint64_t Alignment, uint64_t AlignmentOffset,
                                   bool ZeroInitialize = true) {
    auto Content = allocateBuffer(ContentSize);
    if (ZeroInitialize)
      memset(Content.data(), 0, Content.size());
    return createBlock(Parent, Content, Address, Alignment, AlignmentOffset);
  }

  /// Create a zero-fill block.
  /// \param Parent Section that will own the new block.
  /// \param Size Zero-fill size in bytes.
  /// \param Address Address assigned to the block.
  /// \param Alignment Required alignment of the block.
  /// \param AlignmentOffset Offset applied when aligning the block.
  /// \return A reference to the newly created zero-fill block.
  Block &createZeroFillBlock(Section &Parent, orc::ExecutorAddrDiff Size,
                             orc::ExecutorAddr Address, uint64_t Alignment,
                             uint64_t AlignmentOffset) {
    return createBlock(Parent, Size, Address, Alignment, AlignmentOffset);
  }

  /// Returns a BinaryStreamReader for the given block.
  /// \param B Block whose content should be read.
  /// \return A BinaryStreamReader for the given block.
  BinaryStreamReader getBlockContentReader(Block &B) {
    ArrayRef<uint8_t> C(
        reinterpret_cast<const uint8_t *>(B.getContent().data()), B.getSize());
    return BinaryStreamReader(C, getEndianness());
  }

  /// Returns a BinaryStreamWriter for the given block.
  /// This will call getMutableContent to obtain mutable content for the block.
  /// \param B Block whose content should be written.
  /// \return A BinaryStreamWriter for the given block.
  BinaryStreamWriter getBlockContentWriter(Block &B) {
    MutableArrayRef<uint8_t> C(
        reinterpret_cast<uint8_t *>(B.getMutableContent(*this).data()),
        B.getSize());
    return BinaryStreamWriter(C, getEndianness());
  }

  /// Cache type for the splitBlock function.
  using SplitBlockCache = std::optional<SmallVector<Symbol *, 8>>;

  /// Splits block B into a sequence of smaller blocks.
  ///
  /// SplitOffsets should be a sequence of ascending offsets in B. The starting
  /// offset should be greater than zero, and the final offset less than
  /// B.getSize() - 1.
  ///
  /// The resulting seqeunce of blocks will start with the original block B
  /// (truncated to end at the first split offset) followed by newly introduced
  /// blocks starting at the subsequent split points.
  ///
  /// The optional Cache parameter can be used to speed up repeated calls to
  /// splitBlock for blocks within a single Section. If the value is None then
  /// the cache will be treated as uninitialized and splitBlock will populate
  /// it. Otherwise it is assumed to contain the list of Symbols pointing at B,
  /// sorted in descending order of offset.
  ///
  ///
  /// Notes:
  ///
  /// 1. splitBlock must be used with care. Splitting a block may cause
  ///    incoming edges to become invalid if the edge target subexpression
  ///    points outside the bounds of the newly split target block (E.g. an
  ///    edge 'S + 10 : Pointer64' where S points to a newly split block
  ///    whose size is less than 10). No attempt is made to detect invalidation
  ///    of incoming edges, as in general this requires context that the
  ///    LinkGraph does not have. Clients are responsible for ensuring that
  ///    splitBlock is not used in a way that invalidates edges.
  ///
  /// 2. The newly introduced blocks will have new ordinals that will be higher
  ///    than any other ordinals in the section. Clients are responsible for
  ///    re-assigning block ordinals to restore a compatible order if needed.
  ///
  /// 3. The cache is not automatically updated if new symbols are introduced
  ///    between calls to splitBlock. Any newly introduced symbols may be
  ///    added to the cache manually (descending offset order must be
  ///    preserved), or the cache can be set to None and rebuilt by
  ///    splitBlock on the next call.
  /// \param B Block to split into smaller blocks.
  /// \param SplitOffsets Ascending interior offsets at which to split \p B.
  /// \param Cache Optional per-section symbol cache for repeated splits.
  /// \return The sequence of blocks starting with the truncated original.
  template <typename SplitOffsetRange>
  std::vector<Block *> splitBlock(Block &B, SplitOffsetRange &&SplitOffsets,
                                  LinkGraph::SplitBlockCache *Cache = nullptr) {
    std::vector<Block *> Blocks;
    Blocks.push_back(&B);

    if (std::empty(SplitOffsets))
      return Blocks;

    // Special case zero-fill:
    if (B.isZeroFill()) {
      size_t OrigSize = B.getSize();
      for (Edge::OffsetT Offset : SplitOffsets) {
        assert(Offset > 0 && Offset < B.getSize() &&
               "Split offset must be inside block content");
        Blocks.back()->setZeroFillSize(
            Offset - (Blocks.back()->getAddress() - B.getAddress()));
        Blocks.push_back(&createZeroFillBlock(
            B.getSection(), B.getSize(), B.getAddress() + Offset,
            B.getAlignment(),
            (B.getAlignmentOffset() + Offset) % B.getAlignment()));
      }
      Blocks.back()->setZeroFillSize(
          OrigSize - (Blocks.back()->getAddress() - B.getAddress()));
      return Blocks;
    }

    // Handle content blocks. We'll just create the blocks with their starting
    // address and no content here. The bulk of the work is deferred to
    // splitBlockImpl.
    for (Edge::OffsetT Offset : SplitOffsets) {
      assert(Offset > 0 && Offset < B.getSize() &&
             "Split offset must be inside block content");
      Blocks.push_back(&createContentBlock(
          B.getSection(), ArrayRef<char>(), B.getAddress() + Offset,
          B.getAlignment(),
          (B.getAlignmentOffset() + Offset) % B.getAlignment()));
    }

    return splitBlockImpl(std::move(Blocks), Cache);
  }

  /// Intern the given string in the LinkGraph's SymbolStringPool.
  /// \param SymbolName Symbol name to intern.
  /// \return The interned symbol string pointer.
  orc::SymbolStringPtr intern(StringRef SymbolName) {
    return SSP->intern(SymbolName);
  }

  /// Add an external symbol.
  ///
  /// Some formats (e.g. ELF) allow Symbols to have sizes. For Symbols whose
  /// size is not known, you should substitute '0'.
  /// The IsWeaklyReferenced argument determines whether the symbol must be
  /// present during lookup: Externals that are strongly referenced must be
  /// found or an error will be emitted. Externals that are weakly referenced
  /// are permitted to be undefined, in which case they are assigned an address
  /// of 0.
  /// \param Name Interned external symbol name.
  /// \param Size Symbol size, or 0 if unknown.
  /// \param IsWeaklyReferenced Whether an unresolved symbol is allowed.
  /// \return A reference to the newly added external symbol.
  Symbol &addExternalSymbol(orc::SymbolStringPtr Name,
                            orc::ExecutorAddrDiff Size,
                            bool IsWeaklyReferenced) {
    assert(!ExternalSymbols.contains(orc::NonOwningSymbolStringPtr(Name)) &&
           "Duplicate external symbol");
    auto &Sym = Symbol::constructExternal(
        Allocator, createAddressable(orc::ExecutorAddr(), false),
        std::move(Name), Size, Linkage::Strong, IsWeaklyReferenced);
    ExternalSymbols.insert(
        {orc::NonOwningSymbolStringPtr(Sym.getName()), &Sym});
    return Sym;
  }

  /// Add an external symbol with a raw string name.
  /// \param Name External symbol name to intern.
  /// \param Size Symbol size, or 0 if unknown.
  /// \param IsWeaklyReferenced Whether an unresolved symbol is allowed.
  /// \return A reference to the newly added external symbol.
  Symbol &addExternalSymbol(StringRef Name, orc::ExecutorAddrDiff Size,
                            bool IsWeaklyReferenced) {
    return addExternalSymbol(SSP->intern(Name), Size, IsWeaklyReferenced);
  }

  /// Add an absolute symbol.
  /// \param Name Interned absolute symbol name.
  /// \param Address Absolute address of the symbol.
  /// \param Size Size of the absolute symbol.
  /// \param L Linkage for the symbol.
  /// \param S Scope for the symbol.
  /// \param IsLive Whether the symbol should be treated as live.
  /// \return A reference to the newly added absolute symbol.
  Symbol &addAbsoluteSymbol(orc::SymbolStringPtr Name,
                            orc::ExecutorAddr Address,
                            orc::ExecutorAddrDiff Size, Linkage L, Scope S,
                            bool IsLive) {
    assert((S == Scope::Local || llvm::none_of(AbsoluteSymbols,
                                               [&](const Symbol *Sym) {
                                                 return Sym->getName() == Name;
                                               })) &&
           "Duplicate absolute symbol");
    auto &Sym = Symbol::constructAbsolute(Allocator, createAddressable(Address),
                                          std::move(Name), Size, L, S, IsLive);
    AbsoluteSymbols.insert(&Sym);
    return Sym;
  }

  /// Add an absolute symbol with a raw string name.
  /// \param Name Absolute symbol name to intern.
  /// \param Address Absolute address of the symbol.
  /// \param Size Size of the absolute symbol.
  /// \param L Linkage for the symbol.
  /// \param S Scope for the symbol.
  /// \param IsLive Whether the symbol should be treated as live.
  /// \return A reference to the newly added absolute symbol.
  Symbol &addAbsoluteSymbol(StringRef Name, orc::ExecutorAddr Address,
                            orc::ExecutorAddrDiff Size, Linkage L, Scope S,
                            bool IsLive) {

    return addAbsoluteSymbol(SSP->intern(Name), Address, Size, L, S, IsLive);
  }

  /// Add an anonymous symbol.
  /// \param Content Block that backs the anonymous symbol.
  /// \param Offset Offset of the symbol within \p Content.
  /// \param Size Size of the anonymous symbol.
  /// \param IsCallable Whether the symbol is callable.
  /// \param IsLive Whether the symbol should be treated as live.
  /// \return A reference to the newly added anonymous symbol.
  Symbol &addAnonymousSymbol(Block &Content, orc::ExecutorAddrDiff Offset,
                             orc::ExecutorAddrDiff Size, bool IsCallable,
                             bool IsLive) {
    auto &Sym = Symbol::constructAnonDef(Allocator, Content, Offset, Size,
                                         IsCallable, IsLive);
    Content.getSection().addSymbol(Sym);
    return Sym;
  }

  /// Add a named symbol.
  /// \param Content Block that backs the defined symbol.
  /// \param Offset Offset of the symbol within \p Content.
  /// \param Name Symbol name to intern and assign.
  /// \param Size Size of the defined symbol.
  /// \param L Linkage for the symbol.
  /// \param S Scope for the symbol.
  /// \param IsCallable Whether the symbol is callable.
  /// \param IsLive Whether the symbol should be treated as live.
  /// \return A reference to the newly added defined symbol.
  Symbol &addDefinedSymbol(Block &Content, orc::ExecutorAddrDiff Offset,
                           StringRef Name, orc::ExecutorAddrDiff Size,
                           Linkage L, Scope S, bool IsCallable, bool IsLive) {
    return addDefinedSymbol(Content, Offset, SSP->intern(Name), Size, L, S,
                            IsCallable, IsLive);
  }

  /// Add a named symbol with an already-interned name.
  /// \param Content Block that backs the defined symbol.
  /// \param Offset Offset of the symbol within \p Content.
  /// \param Name Interned symbol name to assign.
  /// \param Size Size of the defined symbol.
  /// \param L Linkage for the symbol.
  /// \param S Scope for the symbol.
  /// \param IsCallable Whether the symbol is callable.
  /// \param IsLive Whether the symbol should be treated as live.
  /// \return A reference to the newly added defined symbol.
  Symbol &addDefinedSymbol(Block &Content, orc::ExecutorAddrDiff Offset,
                           orc::SymbolStringPtr Name,
                           orc::ExecutorAddrDiff Size, Linkage L, Scope S,
                           bool IsCallable, bool IsLive) {
    assert((S == Scope::Local || llvm::none_of(defined_symbols(),
                                               [&](const Symbol *Sym) {
                                                 return Sym->getName() == Name;
                                               })) &&
           "Duplicate defined symbol");
    auto &Sym =
        Symbol::constructNamedDef(Allocator, Content, Offset, std::move(Name),
                                  Size, L, S, IsLive, IsCallable);
    Content.getSection().addSymbol(Sym);
    return Sym;
  }

  /// Returns an iterator range over sections in the graph.
  /// \return An iterator range over sections in the graph.
  iterator_range<section_iterator> sections() {
    return make_range(
        section_iterator(Sections.begin(), GetSectionMapEntryValue()),
        section_iterator(Sections.end(), GetSectionMapEntryValue()));
  }

  /// Returns an iterator range over sections in the graph.
  /// \return An iterator range over sections in the graph.
  iterator_range<const_section_iterator> sections() const {
    return make_range(
        const_section_iterator(Sections.begin(),
                               GetSectionMapEntryConstValue()),
        const_section_iterator(Sections.end(), GetSectionMapEntryConstValue()));
  }

  /// Returns the number of sections in the graph.
  /// \return The number of sections in the graph.
  size_t sections_size() const { return Sections.size(); }

  /// Returns the section with the given name if it exists, otherwise returns
  /// null.
  /// \param Name Section name to look up.
  /// \return The section named \p Name, or null if none exists.
  Section *findSectionByName(StringRef Name) {
    auto I = Sections.find(Name);
    if (I == Sections.end())
      return nullptr;
    return I->second.get();
  }

  /// Returns an iterator range over all blocks in the graph.
  /// \return An iterator range over all blocks in the graph.
  iterator_range<block_iterator> blocks() {
    auto Secs = sections();
    return make_range(block_iterator(Secs.begin(), Secs.end()),
                      block_iterator(Secs.end(), Secs.end()));
  }

  /// Returns an iterator range over all blocks in the graph.
  /// \return An iterator range over all blocks in the graph.
  iterator_range<const_block_iterator> blocks() const {
    auto Secs = sections();
    return make_range(const_block_iterator(Secs.begin(), Secs.end()),
                      const_block_iterator(Secs.end(), Secs.end()));
  }

  /// Returns an iterator range over external symbols in the graph.
  /// \return An iterator range over external symbols in the graph.
  iterator_range<external_symbol_iterator> external_symbols() {
    return make_range(
        external_symbol_iterator(ExternalSymbols.begin(),
                                 GetExternalSymbolMapEntryValue()),
        external_symbol_iterator(ExternalSymbols.end(),
                                 GetExternalSymbolMapEntryValue()));
  }

  /// Returns the external symbol with the given name if one exists, otherwise
  /// returns nullptr.
  /// \param Name Symbol name to search for among external symbols.
  /// \return The external symbol named \p Name, or null if none exists.
  Symbol *findExternalSymbolByName(const orc::SymbolStringPtrBase &Name) {
    for (auto *Sym : external_symbols())
      if (Sym->getName() == Name)
        return Sym;
    return nullptr;
  }

  /// Returns an iterator range over absolute symbols in the graph.
  /// \return An iterator range over absolute symbols in the graph.
  iterator_range<absolute_symbol_iterator> absolute_symbols() {
    return make_range(AbsoluteSymbols.begin(), AbsoluteSymbols.end());
  }

  /// Returns the absolute symbol with the given name if one exists, otherwise
  /// returns nullptr.
  /// \param Name Symbol name to search for among absolute symbols.
  /// \return The absolute symbol named \p Name, or null if none exists.
  Symbol *findAbsoluteSymbolByName(const orc::SymbolStringPtrBase &Name) {
    for (auto *Sym : absolute_symbols())
      if (Sym->getName() == Name)
        return Sym;
    return nullptr;
  }

  /// Returns an iterator range over defined symbols in the graph.
  /// \return An iterator range over defined symbols in the graph.
  iterator_range<defined_symbol_iterator> defined_symbols() {
    auto Secs = sections();
    return make_range(defined_symbol_iterator(Secs.begin(), Secs.end()),
                      defined_symbol_iterator(Secs.end(), Secs.end()));
  }

  /// Returns an iterator range over defined symbols in the graph.
  /// \return An iterator range over defined symbols in the graph.
  iterator_range<const_defined_symbol_iterator> defined_symbols() const {
    auto Secs = sections();
    return make_range(const_defined_symbol_iterator(Secs.begin(), Secs.end()),
                      const_defined_symbol_iterator(Secs.end(), Secs.end()));
  }

  /// Returns the defined symbol with the given name if one exists, otherwise
  /// returns nullptr.
  /// \param Name Symbol name to search for among defined symbols.
  /// \return The defined symbol named \p Name, or null if none exists.
  Symbol *findDefinedSymbolByName(const orc::SymbolStringPtrBase &Name) {
    for (auto *Sym : defined_symbols())
      if (Sym->hasName() && Sym->getName() == Name)
        return Sym;
    return nullptr;
  }

  /// Make the given symbol external (must not already be external).
  ///
  /// Symbol size, linkage and callability will be left unchanged. Symbol scope
  /// will be set to Default, and offset will be reset to 0.
  /// \param Sym Symbol to convert to an external.
  void makeExternal(Symbol &Sym) {
    assert(!Sym.isExternal() && "Symbol is already external");
    if (Sym.isAbsolute()) {
      assert(AbsoluteSymbols.count(&Sym) &&
             "Sym is not in the absolute symbols set");
      assert(Sym.getOffset() == 0 && "Absolute not at offset 0");
      AbsoluteSymbols.erase(&Sym);
      auto &A = Sym.getAddressable();
      A.setAbsolute(false);
      A.setAddress(orc::ExecutorAddr());
    } else {
      assert(Sym.isDefined() && "Sym is not a defined symbol");
      Section &Sec = Sym.getSection();
      Sec.removeSymbol(Sym);
      Sym.makeExternal(createAddressable(orc::ExecutorAddr(), false));
    }
    ExternalSymbols.insert(
        {orc::NonOwningSymbolStringPtr(Sym.getName()), &Sym});
  }

  /// Make the given symbol an absolute with the given address (must not already
  /// be absolute).
  ///
  /// The symbol's size, linkage, and callability, and liveness will be left
  /// unchanged, and its offset will be reset to 0.
  ///
  /// If the symbol was external then its scope will be set to local, otherwise
  /// it will be left unchanged.
  /// \param Sym Symbol to convert to an absolute.
  /// \param Address Absolute address to assign.
  void makeAbsolute(Symbol &Sym, orc::ExecutorAddr Address) {
    assert(!Sym.isAbsolute() && "Symbol is already absolute");
    if (Sym.isExternal()) {
      assert(ExternalSymbols.contains(
                 orc::NonOwningSymbolStringPtr(Sym.getName())) &&
             "Sym is not in the absolute symbols set");
      assert(Sym.getOffset() == 0 && "External is not at offset 0");
      ExternalSymbols.erase(orc::NonOwningSymbolStringPtr(Sym.getName()));
      auto &A = Sym.getAddressable();
      A.setAbsolute(true);
      A.setAddress(Address);
      Sym.setScope(Scope::Local);
    } else {
      assert(Sym.isDefined() && "Sym is not a defined symbol");
      Section &Sec = Sym.getSection();
      Sec.removeSymbol(Sym);
      Sym.makeAbsolute(createAddressable(Address));
    }
    AbsoluteSymbols.insert(&Sym);
  }

  /// Turn an absolute or external symbol into a defined one by attaching it to
  /// a block. Symbol must not already be defined.
  /// \param Sym Absolute or external symbol to convert.
  /// \param Content Block that will back the defined symbol.
  /// \param Offset Offset of the symbol within \p Content.
  /// \param Size Size of the defined symbol.
  /// \param L Linkage to assign to the symbol.
  /// \param S Scope to assign to the symbol.
  /// \param IsLive Whether the symbol should be treated as live.
  void makeDefined(Symbol &Sym, Block &Content, orc::ExecutorAddrDiff Offset,
                   orc::ExecutorAddrDiff Size, Linkage L, Scope S,
                   bool IsLive) {
    assert(!Sym.isDefined() && "Sym is already a defined symbol");
    if (Sym.isAbsolute()) {
      assert(AbsoluteSymbols.count(&Sym) &&
             "Symbol is not in the absolutes set");
      AbsoluteSymbols.erase(&Sym);
    } else {
      assert(ExternalSymbols.contains(
                 orc::NonOwningSymbolStringPtr(Sym.getName())) &&
             "Symbol is not in the externals set");
      ExternalSymbols.erase(orc::NonOwningSymbolStringPtr(Sym.getName()));
    }
    Addressable &OldBase = *Sym.Base;
    Sym.setBlock(Content);
    Sym.setOffset(Offset);
    Sym.setSize(Size);
    Sym.setLinkage(L);
    Sym.setScope(S);
    Sym.setLive(IsLive);
    Content.getSection().addSymbol(Sym);
    destroyAddressable(OldBase);
  }

  /// Transfer a defined symbol from one block to another.
  ///
  /// The symbol's offset within DestBlock is set to NewOffset.
  ///
  /// If ExplicitNewSize is given as None then the size of the symbol will be
  /// checked and auto-truncated to at most the size of the remainder (from the
  /// given offset) of the size of the new block.
  ///
  /// All other symbol attributes are unchanged.
  /// \param Sym Defined symbol to reattach.
  /// \param DestBlock Block that will own the symbol.
  /// \param NewOffset Offset of the symbol within \p DestBlock.
  /// \param ExplicitNewSize Optional new size; otherwise truncated to fit.
  void
  transferDefinedSymbol(Symbol &Sym, Block &DestBlock,
                        orc::ExecutorAddrDiff NewOffset,
                        std::optional<orc::ExecutorAddrDiff> ExplicitNewSize) {
    auto &OldSection = Sym.getSection();
    Sym.setBlock(DestBlock);
    Sym.setOffset(NewOffset);
    if (ExplicitNewSize)
      Sym.setSize(*ExplicitNewSize);
    else {
      auto RemainingBlockSize = DestBlock.getSize() - NewOffset;
      if (Sym.getSize() > RemainingBlockSize)
        Sym.setSize(RemainingBlockSize);
    }
    if (&DestBlock.getSection() != &OldSection) {
      OldSection.removeSymbol(Sym);
      DestBlock.getSection().addSymbol(Sym);
    }
  }

  /// Transfers the given Block and all Symbols pointing to it to the given
  /// Section.
  ///
  /// No attempt is made to check compatibility of the source and destination
  /// sections. Blocks may be moved between sections with incompatible
  /// permissions (e.g. from data to text). The client is responsible for
  /// ensuring that this is safe.
  /// \param B Block to move, along with symbols that point to it.
  /// \param NewSection Destination section for the block and symbols.
  void transferBlock(Block &B, Section &NewSection) {
    auto &OldSection = B.getSection();
    if (&OldSection == &NewSection)
      return;
    SmallVector<Symbol *> AttachedSymbols;
    for (auto *S : OldSection.symbols())
      if (&S->getBlock() == &B)
        AttachedSymbols.push_back(S);
    for (auto *S : AttachedSymbols) {
      OldSection.removeSymbol(*S);
      NewSection.addSymbol(*S);
    }
    OldSection.removeBlock(B);
    NewSection.addBlock(B);
  }

  /// Move all blocks and symbols from the source section to the destination
  /// section.
  ///
  /// If PreserveSrcSection is true (or SrcSection and DstSection are the same)
  /// then SrcSection is preserved, otherwise it is removed (the default).
  /// \param DstSection Section that receives the content.
  /// \param SrcSection Section whose content is moved.
  /// \param PreserveSrcSection If true, keep an empty source section.
  void mergeSections(Section &DstSection, Section &SrcSection,
                     bool PreserveSrcSection = false) {
    if (&DstSection == &SrcSection)
      return;
    for (auto *B : SrcSection.blocks())
      B->setSection(DstSection);
    SrcSection.transferContentTo(DstSection);
    if (!PreserveSrcSection)
      removeSection(SrcSection);
  }

  /// Removes an external symbol. Also removes the underlying Addressable.
  /// \param Sym External symbol to remove from the graph.
  void removeExternalSymbol(Symbol &Sym) {
    assert(!Sym.isDefined() && !Sym.isAbsolute() &&
           "Sym is not an external symbol");
    assert(ExternalSymbols.contains(
               orc::NonOwningSymbolStringPtr(Sym.getName())) &&
           "Symbol is not in the externals set");
    ExternalSymbols.erase(orc::NonOwningSymbolStringPtr(Sym.getName()));
    Addressable &Base = *Sym.Base;
    assert(llvm::none_of(external_symbols(),
                         [&](Symbol *AS) { return AS->Base == &Base; }) &&
           "Base addressable still in use");
    destroySymbol(Sym);
    destroyAddressable(Base);
  }

  /// Remove an absolute symbol. Also removes the underlying Addressable.
  /// \param Sym Absolute symbol to remove from the graph.
  void removeAbsoluteSymbol(Symbol &Sym) {
    assert(!Sym.isDefined() && Sym.isAbsolute() &&
           "Sym is not an absolute symbol");
    assert(AbsoluteSymbols.count(&Sym) &&
           "Symbol is not in the absolute symbols set");
    AbsoluteSymbols.erase(&Sym);
    Addressable &Base = *Sym.Base;
    assert(llvm::none_of(external_symbols(),
                         [&](Symbol *AS) { return AS->Base == &Base; }) &&
           "Base addressable still in use");
    destroySymbol(Sym);
    destroyAddressable(Base);
  }

  /// Removes defined symbols. Does not remove the underlying block.
  /// \param Sym Defined symbol to remove from the graph.
  void removeDefinedSymbol(Symbol &Sym) {
    assert(Sym.isDefined() && "Sym is not a defined symbol");
    Sym.getSection().removeSymbol(Sym);
    destroySymbol(Sym);
  }

  /// Remove a block. The block reference is defunct after calling this
  /// function and should no longer be used.
  /// \param B Block to remove from its section and destroy.
  void removeBlock(Block &B) {
    assert(llvm::none_of(B.getSection().symbols(),
                         [&](const Symbol *Sym) {
                           return &Sym->getBlock() == &B;
                         }) &&
           "Block still has symbols attached");
    B.getSection().removeBlock(B);
    destroyBlock(B);
  }

  /// Remove a section. The section reference is defunct after calling this
  /// function and should no longer be used.
  /// \param Sec Section to remove from the graph.
  void removeSection(Section &Sec) {
    assert(Sections.count(Sec.getName()) && "Section not found");
    assert(Sections.find(Sec.getName())->second.get() == &Sec &&
           "Section map entry invalid");
    Sections.erase(Sec.getName());
  }

  /// Accessor for the AllocActions object for this graph. This can be used to
  /// register allocation action calls prior to finalization.
  ///
  /// Accessing this object after finalization will result in undefined
  /// behavior.
  /// \return The allocation-actions list for this graph.
  orc::shared::AllocActions &allocActions() { return AAs; }

  /// Dump the graph.
  /// \param OS Output stream to write the dump to.
  LLVM_ABI void dump(raw_ostream &OS);

private:
  LLVM_ABI std::vector<Block *> splitBlockImpl(std::vector<Block *> Blocks,
                                               SplitBlockCache *Cache);

  // Put the BumpPtrAllocator first so that we don't free any of the underlying
  // memory until the Symbol/Addressable destructors have been run.
  BumpPtrAllocator Allocator;

  std::string Name;
  std::shared_ptr<orc::SymbolStringPool> SSP;
  Triple TT;
  SubtargetFeatures Features;
  GetEdgeKindNameFunction GetEdgeKindName = nullptr;
  DenseMap<StringRef, std::unique_ptr<Section>> Sections;
  ExternalSymbolMap ExternalSymbols;
  AbsoluteSymbolSet AbsoluteSymbols;
  orc::shared::AllocActions AAs;
};

/// Return mutable content for this block, copying if needed.
/// \param G Link graph whose allocator is used if content must be copied.
/// \return Mutable content for this block, copying if needed.
inline MutableArrayRef<char> Block::getMutableContent(LinkGraph &G) {
  if (!ContentMutable)
    setMutableContent(G.allocateContent({Data, Size}));
  return MutableArrayRef<char>(const_cast<char *>(Data), Size);
}

/// Enables easy lookup of blocks by addresses.
class BlockAddressMap {
public:
  /// Map from block start address to block pointer.
  using AddrToBlockMap = std::map<orc::ExecutorAddr, Block *>;
  /// Const iterator over address-to-block map entries.
  using const_iterator = AddrToBlockMap::const_iterator;

  /// A block predicate that always adds all blocks.
  /// \param B Block being considered for inclusion.
  /// \return True for every block.
  static bool includeAllBlocks(const Block &B) { return true; }

  /// A block predicate that always includes blocks with non-null addresses.
  /// \param B Block being considered for inclusion.
  /// \return True if \p B has a non-null address.
  static bool includeNonNull(const Block &B) { return !!B.getAddress(); }

  /// Create an empty block address map.
  BlockAddressMap() = default;

  /// Add a block to the map. Returns an error if the block overlaps with any
  /// existing block.
  /// \param B Block to insert into the map.
  /// \param Pred Predicate selecting whether \p B should be added.
  /// \return Success, or an error if \p B overlaps an existing block.
  template <typename PredFn = decltype(includeAllBlocks)>
  Error addBlock(Block &B, PredFn Pred = includeAllBlocks) {
    if (!Pred(B))
      return Error::success();

    auto I = AddrToBlock.upper_bound(B.getAddress());

    // If we're not at the end of the map, check for overlap with the next
    // element.
    if (I != AddrToBlock.end()) {
      if (B.getAddress() + B.getSize() > I->second->getAddress())
        return overlapError(B, *I->second);
    }

    // If we're not at the start of the map, check for overlap with the previous
    // element.
    if (I != AddrToBlock.begin()) {
      auto &PrevBlock = *std::prev(I)->second;
      if (PrevBlock.getAddress() + PrevBlock.getSize() > B.getAddress())
        return overlapError(B, PrevBlock);
    }

    AddrToBlock.insert(I, std::make_pair(B.getAddress(), &B));
    return Error::success();
  }

  /// Add a block without overlap checking.
  ///
  /// Add a block to the map without checking for overlap with existing blocks.
  /// The client is responsible for ensuring that the block added does not
  /// overlap with any existing block.
  /// \param B Block to insert into the map.
  void addBlockWithoutChecking(Block &B) { AddrToBlock[B.getAddress()] = &B; }

  /// Add a range of blocks to the map. Returns an error if any block in the
  /// range overlaps with any other block in the range, or with any existing
  /// block in the map.
  /// \param Blocks Range of block pointers to insert.
  /// \param Pred Predicate selecting which blocks should be added.
  /// \return Success, or an error if any added block overlaps another block.
  template <typename BlockPtrRange,
            typename PredFn = decltype(includeAllBlocks)>
  Error addBlocks(BlockPtrRange &&Blocks, PredFn Pred = includeAllBlocks) {
    for (auto *B : Blocks)
      if (auto Err = addBlock(*B, Pred))
        return Err;
    return Error::success();
  }

  /// Add blocks without overlap checking.
  ///
  /// Add a range of blocks to the map without checking for overlap with
  /// existing blocks. The client is responsible for ensuring that the block
  /// added does not overlap with any existing block.
  /// \param Blocks Range of block pointers to insert.
  template <typename BlockPtrRange>
  void addBlocksWithoutChecking(BlockPtrRange &&Blocks) {
    for (auto *B : Blocks)
      addBlockWithoutChecking(*B);
  }

  /// Iterates over (Address, Block*) pairs in ascending order of address.
  /// \return An iterator to the first (address, block) pair.
  const_iterator begin() const { return AddrToBlock.begin(); }
  /// Returns an iterator to the end of the address-to-block map.
  /// \return An iterator past the last (address, block) pair.
  const_iterator end() const { return AddrToBlock.end(); }

  /// Returns the block starting at the given address, or nullptr if no such
  /// block exists.
  /// \param Addr Start address of the block to find.
  /// \return The block starting at \p Addr, or null if none exists.
  Block *getBlockAt(orc::ExecutorAddr Addr) const {
    auto I = AddrToBlock.find(Addr);
    if (I == AddrToBlock.end())
      return nullptr;
    return I->second;
  }

  /// Returns the block covering the given address, or nullptr if no such block
  /// exists.
  /// \param Addr Address that must fall within the returned block's range.
  /// \return The block covering \p Addr, or null if none exists.
  Block *getBlockCovering(orc::ExecutorAddr Addr) const {
    auto I = AddrToBlock.upper_bound(Addr);
    if (I == AddrToBlock.begin())
      return nullptr;
    auto *B = std::prev(I)->second;
    if (Addr < B->getAddress() + B->getSize())
      return B;
    return nullptr;
  }

private:
  Error overlapError(Block &NewBlock, Block &ExistingBlock) {
    auto NewBlockEnd = NewBlock.getAddress() + NewBlock.getSize();
    auto ExistingBlockEnd =
        ExistingBlock.getAddress() + ExistingBlock.getSize();
    return make_error<JITLinkError>(
        "Block at " +
        formatv("{0:x16} -- {1:x16}", NewBlock.getAddress().getValue(),
                NewBlockEnd.getValue()) +
        " overlaps " +
        formatv("{0:x16} -- {1:x16}", ExistingBlock.getAddress().getValue(),
                ExistingBlockEnd.getValue()));
  }

  AddrToBlockMap AddrToBlock;
};

/// A map of addresses to Symbols.
class SymbolAddressMap {
public:
  /// Vector of symbols that share a single address.
  using SymbolVector = SmallVector<Symbol *, 1>;

  /// Add a symbol to the SymbolAddressMap.
  /// \param Sym Symbol whose address is recorded in the map.
  void addSymbol(Symbol &Sym) {
    AddrToSymbols[Sym.getAddress()].push_back(&Sym);
  }

  /// Add all symbols in a given range to the SymbolAddressMap.
  /// \param Symbols Range of symbol pointers to insert.
  template <typename SymbolPtrCollection>
  void addSymbols(SymbolPtrCollection &&Symbols) {
    for (auto *Sym : Symbols)
      addSymbol(*Sym);
  }

  /// Returns the list of symbols that start at the given address, or nullptr if
  /// no such symbols exist.
  /// \param Addr Address whose symbols should be returned.
  /// \return The symbols that start at \p Addr, or null if none exist.
  const SymbolVector *getSymbolsAt(orc::ExecutorAddr Addr) const {
    auto I = AddrToSymbols.find(Addr);
    if (I == AddrToSymbols.end())
      return nullptr;
    return &I->second;
  }

private:
  std::map<orc::ExecutorAddr, SymbolVector> AddrToSymbols;
};

/// A function for mutating LinkGraphs.
using LinkGraphPassFunction = unique_function<Error(LinkGraph &)>;

/// A list of LinkGraph passes.
using LinkGraphPassList = std::vector<LinkGraphPassFunction>;

/// An LinkGraph pass configuration, consisting of a list of pre-prune,
/// post-prune, and post-fixup passes.
struct PassConfiguration {

  /// Pre-prune passes.
  ///
  /// These passes are called on the graph after it is built, and before any
  /// symbols have been pruned. Graph nodes still have their original vmaddrs.
  ///
  /// Notable use cases: Marking symbols live or should-discard.
  LinkGraphPassList PrePrunePasses;

  /// Post-prune passes.
  ///
  /// These passes are called on the graph after dead stripping, but before
  /// memory is allocated or nodes assigned their final addresses.
  ///
  /// Notable use cases: Building GOT, stub, and TLV symbols.
  LinkGraphPassList PostPrunePasses;

  /// Post-allocation passes.
  ///
  /// These passes are called on the graph after memory has been allocated and
  /// defined nodes have been assigned their final addresses, but before the
  /// context has been notified of these addresses. At this point externals
  /// have not been resolved, and symbol content has not yet been copied into
  /// working memory.
  ///
  /// Notable use cases: Setting up data structures associated with addresses
  /// of defined symbols (e.g. a mapping of __dso_handle to JITDylib* for the
  /// JIT runtime) -- using a PostAllocationPass for this ensures that the
  /// data structures are in-place before any query for resolved symbols
  /// can complete.
  LinkGraphPassList PostAllocationPasses;

  /// Pre-fixup passes.
  ///
  /// These passes are called on the graph after memory has been allocated,
  /// content copied into working memory, and all nodes (including externals)
  /// have been assigned their final addresses, but before any fixups have been
  /// applied.
  ///
  /// Notable use cases: Late link-time optimizations like GOT and stub
  /// elimination.
  LinkGraphPassList PreFixupPasses;

  /// Post-fixup passes.
  ///
  /// These passes are called on the graph after block contents has been copied
  /// to working memory, and fixups applied. Blocks have been updated to point
  /// to their fixed up content.
  ///
  /// Notable use cases: Testing and validation.
  LinkGraphPassList PostFixupPasses;
};

/// Flags for symbol lookup.
///
/// FIXME: These basically duplicate orc::SymbolLookupFlags -- We should merge
///        the two types once we have an OrcSupport library.
enum class SymbolLookupFlags {
  RequiredSymbol,         ///< Symbol must resolve or linking fails.
  WeaklyReferencedSymbol, ///< Symbol may be unresolved; address becomes zero.
};

/// Write the given symbol lookup flags to an output stream.
/// \param OS Output stream to write to.
/// \param LF Lookup flags value to print.
/// \return A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const SymbolLookupFlags &LF);

/// A map of symbol names to resolved addresses.
using AsyncLookupResult =
    DenseMap<orc::SymbolStringPtr, orc::ExecutorSymbolDef>;

/// A function object to call with a resolved symbol map (See AsyncLookupResult)
/// or an error if resolution failed.
class LLVM_ABI JITLinkAsyncLookupContinuation {
public:
  /// Destroy a lookup continuation.
  virtual ~JITLinkAsyncLookupContinuation() = default;
  /// Continue linking with the given lookup result or error.
  /// \param LR Resolved symbols, or an error if lookup failed.
  virtual void run(Expected<AsyncLookupResult> LR) = 0;

private:
  virtual void anchor();
};

/// Create a lookup continuation from a function object.
/// \param Cont Callable invoked with the async lookup result.
/// \return A lookup continuation that invokes \p Cont with the result.
template <typename Continuation>
std::unique_ptr<JITLinkAsyncLookupContinuation>
createLookupContinuation(Continuation Cont) {

  class Impl final : public JITLinkAsyncLookupContinuation {
  public:
    Impl(Continuation C) : C(std::move(C)) {}
    void run(Expected<AsyncLookupResult> LR) override { C(std::move(LR)); }

  private:
    Continuation C;
  };

  return std::make_unique<Impl>(std::move(Cont));
}

/// Holds context for a single jitLink invocation.
class LLVM_ABI JITLinkContext {
public:
  /// Map of symbol names to lookup flags for an async lookup request.
  using LookupMap = DenseMap<orc::SymbolStringPtr, SymbolLookupFlags>;

  /// Create a JITLinkContext.
  /// \param JD Target JITLink dylib for this link, or null if none.
  JITLinkContext(const JITLinkDylib *JD) : JD(JD) {}

  /// Destroy a JITLinkContext.
  virtual ~JITLinkContext();

  /// Return the JITLinkDylib that this link is targeting, if any.
  /// \return The target JITLink dylib, or null if none.
  const JITLinkDylib *getJITLinkDylib() const { return JD; }

  /// Return the MemoryManager to be used for this link.
  /// \return The memory manager used for this link.
  virtual JITLinkMemoryManager &getMemoryManager() = 0;

  /// Notify this context that linking failed.
  /// Called by JITLink if linking cannot be completed.
  /// \param Err Error describing why linking failed.
  virtual void notifyFailed(Error Err) = 0;

  /// Called by JITLink to resolve external symbols. This method is passed a
  /// lookup continutation which it must call with a result to continue the
  /// linking process.
  /// \param Symbols Symbols to look up and their required lookup flags.
  /// \param LC Continuation to invoke with the lookup result.
  virtual void lookup(const LookupMap &Symbols,
                      std::unique_ptr<JITLinkAsyncLookupContinuation> LC) = 0;

  /// Notify that defined symbols have been assigned final addresses.
  ///
  /// Called by JITLink once all defined symbols in the graph have been assigned
  /// their final memory locations in the target process. At this point the
  /// LinkGraph can be inspected to build a symbol table, however the block
  /// content will not generally have been copied to the target location yet.
  ///
  /// If the client detects an error in the LinkGraph state (e.g. unexpected or
  /// missing symbols) they may return an error here. The error will be
  /// propagated to notifyFailed and the linker will bail out.
  /// \param G Link graph whose defined symbols have been resolved.
  /// \return Success, or an error if the resolved graph state is invalid.
  virtual Error notifyResolved(LinkGraph &G) = 0;

  /// Notify that the object has been finalized in target memory.
  ///
  /// Called by JITLink to notify the context that the object has been
  /// finalized (i.e. emitted to memory and memory permissions set). If all of
  /// this objects dependencies have also been finalized then the code is ready
  /// to run.
  /// \param Alloc Finalized allocation owning the emitted object memory.
  virtual void notifyFinalized(JITLinkMemoryManager::FinalizedAlloc Alloc) = 0;

  /// Return whether default target passes should be added for this link.
  ///
  /// Called by JITLink prior to linking to determine whether default passes for
  /// the target should be added. The default implementation returns true.
  /// If subclasses override this method to return false for any target then
  /// they are required to fully configure the pass pipeline for that target.
  /// \param TT Target triple for the link graph being linked.
  /// \return True if default target passes should be added for \p TT.
  virtual bool shouldAddDefaultTargetPasses(const Triple &TT) const;

  /// Return the mark-live pass for this link, if any.
  ///
  /// Returns the mark-live pass to be used for this link. If no pass is
  /// returned (the default) then the target-specific linker implementation will
  /// choose a conservative default (usually marking all symbols live).
  /// This function is only called if shouldAddDefaultTargetPasses returns true,
  /// otherwise the JITContext is responsible for adding a mark-live pass in
  /// modifyPassConfig.
  /// \param TT Target triple for the link graph being linked.
  /// \return The mark-live pass to use, or an empty pass function for the default.
  virtual LinkGraphPassFunction getMarkLivePass(const Triple &TT) const;

  /// Called by JITLink to modify the pass pipeline prior to linking.
  /// The default version performs no modification.
  /// \param G Link graph about to be linked.
  /// \param Config Pass pipeline configuration to adjust.
  /// \return Success, or an error if the pipeline cannot be modified.
  virtual Error modifyPassConfig(LinkGraph &G, PassConfiguration &Config);

private:
  const JITLinkDylib *JD = nullptr;
};

/// Marks all symbols in a graph live. This can be used as a default,
/// conservative mark-live implementation.
/// \param G Link graph whose symbols are marked live.
/// \return Success, or an error if marking symbols live fails.
LLVM_ABI Error markAllSymbolsLive(LinkGraph &G);

/// Create an out of range error for the given edge in the given block.
/// \param G Link graph that owns the block and edge.
/// \param B Block containing the out-of-range edge.
/// \param E Edge whose target is out of range.
/// \return An error describing the out-of-range edge.
LLVM_ABI Error makeTargetOutOfRangeError(const LinkGraph &G, const Block &B,
                                         const Edge &E);

/// Create an alignment error for a fixup at the given location.
/// \param Loc Address of the misaligned fixup.
/// \param Value Value that failed the alignment check.
/// \param N Required alignment in bytes.
/// \param E Edge associated with the alignment failure.
/// \return An error describing the misaligned fixup.
LLVM_ABI Error makeAlignmentError(llvm::orc::ExecutorAddr Loc, uint64_t Value,
                                  int N, const Edge &E);

/// Creates a new pointer block in the given section and returns an
/// Anonymous symbol pointing to it.
///
/// The pointer block will have the following default values:
///   alignment: PointerSize
///   alignment-offset: 0
///   address: highest allowable
using AnonymousPointerCreator =
    unique_function<Symbol &(LinkGraph &G, Section &PointerSection,
                             Symbol *InitialTarget, uint64_t InitialAddend)>;

/// Get target-specific AnonymousPointerCreator
/// \param TT Target triple selecting the pointer-creation implementation.
/// \return A target-specific anonymous pointer creator for \p TT.
LLVM_ABI AnonymousPointerCreator getAnonymousPointerCreator(const Triple &TT);

/// Create a jump stub that jumps via the pointer at the given symbol and
/// an anonymous symbol pointing to it. Return the anonymous symbol.
///
/// The stub block will be created by createPointerJumpStubBlock.
using PointerJumpStubCreator = unique_function<Symbol &(
    LinkGraph &G, Section &StubSection, Symbol &PointerSymbol)>;

/// Get target-specific PointerJumpStubCreator
/// \param TT Target triple selecting the stub-creation implementation.
/// \return A target-specific pointer jump-stub creator for \p TT.
LLVM_ABI PointerJumpStubCreator getPointerJumpStubCreator(const Triple &TT);

/// Base case for edge-visitors where the visitor-list is empty.
/// \param G Link graph containing the edge.
/// \param B Block that owns the edge, or null if none.
/// \param E Edge being visited.
inline void visitEdge(LinkGraph &G, Block *B, Edge &E) {}

/// Apply visitors to an edge until one handles it.
///
/// Applies the first visitor in the list to the given edge. If the visitor's
/// visitEdge method returns true then we return immediately, otherwise we
/// apply the next visitor.
/// \param G Link graph containing the edge.
/// \param B Block that owns the edge, or null if none.
/// \param E Edge being visited.
/// \param V First visitor to apply.
/// \param Vs Remaining visitors to try if \p V does not handle the edge.
template <typename VisitorT, typename... VisitorTs>
void visitEdge(LinkGraph &G, Block *B, Edge &E, VisitorT &&V,
               VisitorTs &&...Vs) {
  if (!V.visitEdge(G, B, E))
    visitEdge(G, B, E, std::forward<VisitorTs>(Vs)...);
}

/// For each edge in the given graph, apply a list of visitors to the edge,
/// stopping when the first visitor's visitEdge method returns true.
///
/// Only visits edges that were in the graph at call time: if any visitor
/// adds new edges those will not be visited. Visitors are not allowed to
/// remove edges (though they can change their kind, target, and addend).
/// \param G Link graph whose existing edges are visited.
/// \param Vs Visitors applied to each edge in order.
template <typename... VisitorTs>
void visitExistingEdges(LinkGraph &G, VisitorTs &&...Vs) {
  // We may add new blocks during this process, but we don't want to iterate
  // over them, so build a worklist.
  std::vector<Block *> Worklist(G.blocks().begin(), G.blocks().end());

  for (auto *B : Worklist)
    for (auto &E : B->edges())
      visitEdge(G, B, E, std::forward<VisitorTs>(Vs)...);
}

/// Create a LinkGraph from the given object buffer.
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
/// \param ObjectBuffer Buffer containing the relocatable object.
/// \param SSP Symbol string pool used to intern symbol names in the graph.
/// \return A LinkGraph for the object, or an error if parsing fails.
LLVM_ABI Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromObject(MemoryBufferRef ObjectBuffer,
                          std::shared_ptr<orc::SymbolStringPool> SSP);

/// Create a \c LinkGraph defining the given absolute symbols.
/// \param TT Target triple for the new graph.
/// \param SSP Symbol string pool used to intern symbol names in the graph.
/// \param Symbols Absolute symbols to define in the graph.
/// \return A LinkGraph defining the given absolute symbols.
LLVM_ABI std::unique_ptr<LinkGraph>
absoluteSymbolsLinkGraph(Triple TT, std::shared_ptr<orc::SymbolStringPool> SSP,
                         orc::SymbolMap Symbols);

/// Link the given graph.
/// \param G Link graph to link.
/// \param Ctx JITLink context providing memory management and callbacks.
LLVM_ABI void link(std::unique_ptr<LinkGraph> G,
                   std::unique_ptr<JITLinkContext> Ctx);

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_JITLINK_H
