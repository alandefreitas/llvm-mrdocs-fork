//===- MCPseudoProbe.h - Pseudo probe encoding support ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCPseudoProbe to support the pseudo
// probe encoding for AutoFDO. Pseudo probes together with their inline context
// are encoded in a DFS recursive way in the .pseudoprobe sections. For each
// .pseudoprobe section, the encoded binary data consist of a single or mutiple
// function records each for one outlined function. A function record has the
// following format :
//
// FUNCTION BODY (one for each outlined function present in the text section)
//    GUID (uint64)
//        GUID of the function's source name which may be different from the
//        actual binary linkage name. This GUID will be used to decode and
//        generate a profile against the source function name.
//    NPROBES (ULEB128)
//        Number of probes originating from this function.
//    NUM_INLINED_FUNCTIONS (ULEB128)
//        Number of callees inlined into this function, aka number of
//        first-level inlinees
//    PROBE RECORDS
//        A list of NPROBES entries. Each entry contains:
//          INDEX (ULEB128)
//          TYPE (uint4)
//            0 - block probe, 1 - indirect call, 2 - direct call
//          ATTRIBUTE (uint3)
//            1 - reserved
//            2 - Sentinel
//            4 - HasDiscriminator
//          ADDRESS_TYPE (uint1)
//            0 - code address for regular probes (for downwards compatibility)
//              - GUID of linkage name for sentinel probes
//            1 - address delta
//          CODE_ADDRESS (uint64 or ULEB128)
//            code address or address delta, depending on ADDRESS_TYPE
//          DISCRIMINATOR (ULEB128) if HasDiscriminator
//    INLINED FUNCTION RECORDS
//        A list of NUM_INLINED_FUNCTIONS entries describing each of the inlined
//        callees.  Each record contains:
//          INLINE SITE
//            ID of the callsite probe (ULEB128)
//          FUNCTION BODY
//            A FUNCTION BODY entry describing the inlined function.
//
// TODO: retire the ADDRESS_TYPE encoding for code addresses once compatibility
// is no longer an issue.
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPSEUDOPROBE_H
#define LLVM_MC_MCPSEUDOPROBE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/IR/PseudoProbe.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorOr.h"
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace llvm {

class MCSymbol;
class MCObjectStreamer;
class raw_ostream;

/// Flags that control how a pseudo probe is encoded.
enum class MCPseudoProbeFlag {
  /// Encode the probe as an address delta rather than an absolute code address.
  AddressDelta = 0x1,
};

/// Function descriptor decoded from a `.pseudo_probe_desc` section.
struct MCPseudoProbeFuncDesc {
  /// GUID of the function's source name.
  uint64_t FuncGUID = 0;
  /// Content hash of the function used for profile matching.
  uint64_t FuncHash = 0;
  /// Source name of the function.
  StringRef FuncName;

  /// Construct a descriptor from \p GUID, \p Hash, and \p Name.
  ///
  /// \param GUID - Function GUID.
  /// \param Hash - Function content hash.
  /// \param Name - Function source name.
  MCPseudoProbeFuncDesc(uint64_t GUID, uint64_t Hash, StringRef Name)
      : FuncGUID(GUID), FuncHash(Hash), FuncName(Name){};

  /// Print this function descriptor to \p OS.
  ///
  /// \param OS - Stream to print to.
  LLVM_ABI void print(raw_ostream &OS);
};

class MCDecodedPseudoProbe;

/// An inline frame as the pair `<CalleeGuid, ProbeID>`.
using InlineSite = std::tuple<uint64_t, uint32_t>;
/// Stack of inline sites from outermost caller to innermost callee.
using MCPseudoProbeInlineStack = SmallVector<InlineSite, 8>;
/// Sorted map from function GUID to `MCPseudoProbeFuncDesc`.
///
/// Inherits vector storage privately and re-exports the public
/// \c std::vector interface with documentation.
class GUIDProbeFunctionMap : private std::vector<MCPseudoProbeFuncDesc> {
  using Base = std::vector<MCPseudoProbeFuncDesc>;

public:
  /// Assign new contents, replacing existing descriptors.
  using Base::assign;
  /// Access the descriptor at index \p N with bounds checking.
  using Base::at;
  /// Return a reference to the last descriptor.
  using Base::back;
  /// Return an iterator to the first descriptor.
  using Base::begin;
  /// Return a pointer to the underlying descriptor storage.
  using Base::data;
  /// Return an iterator past the last descriptor.
  using Base::end;
  /// Erase descriptors from the map.
  using Base::erase;
  /// Return a reference to the first descriptor.
  using Base::front;
  /// Insert descriptors into the map.
  using Base::insert;
  /// Assign from another GUID-to-descriptor map.
  using Base::operator=;
  /// Access the descriptor at index \p N without bounds checking.
  using Base::operator[];
  /// Append a descriptor to the end.
  using Base::push_back;
  /// Return a reverse iterator to the last descriptor.
  using Base::rbegin;
  /// Return a reverse iterator past the first descriptor.
  using Base::rend;
  /// Resize the map to hold \p N descriptors.
  using Base::resize;
  /// Reserve storage for at least \p N descriptors.
  using Base::reserve;
  /// Append a descriptor constructed in place.
  using Base::emplace_back;
  /// Return the number of descriptors.
  using Base::size;
  /// Return whether the map has no descriptors.
  using Base::empty;

  /// Find the descriptor whose GUID equals \p GUID.
  ///
  /// \param GUID - Function GUID to look up.
  /// \return Iterator to the matching descriptor, or \c end() if none.
  auto find(uint64_t GUID) const {
    auto CompareDesc = [](const MCPseudoProbeFuncDesc &Desc, uint64_t GUID) {
      return Desc.FuncGUID < GUID;
    };
    auto It = llvm::lower_bound(*this, GUID, CompareDesc);
    if (It->FuncGUID != GUID)
      return end();
    return It;
  }
};

class MCDecodedPseudoProbeInlineTree;

/// Shared state for an encoded or decoded pseudo probe.
class MCPseudoProbeBase {
protected:
  /// Probe index within the originating function.
  uint32_t Index;
  /// Source discriminator associated with the probe, if any.
  uint32_t Discriminator;
  /// Bitmask of probe attributes (see `PseudoProbeAttributes`).
  uint8_t Attributes;
  /// Probe kind (block, indirect call, or direct call).
  uint8_t Type;
  /// First valid user probe index.
  ///
  /// Equal to `PseudoProbeReservedId::Last + 1` from SampleProfileProbe.h.
  /// That header is not included here to keep MC independent of IPO.
  const static uint32_t PseudoProbeFirstId = 1;

public:
  /// Construct a probe with index, attributes, type, and discriminator.
  ///
  /// \param I - Probe index.
  /// \param At - Attribute bitmask.
  /// \param T - Probe type.
  /// \param D - Discriminator value.
  MCPseudoProbeBase(uint64_t I, uint64_t At, uint8_t T, uint32_t D)
      : Index(I), Discriminator(D), Attributes(At), Type(T) {}

  /// Return true if this is the function-entry probe.
  ///
  /// \return True if this is the function-entry probe.
  bool isEntry() const { return Index == PseudoProbeFirstId; }

  /// Return the probe index within its function.
  ///
  /// \return Probe index within its function.
  uint32_t getIndex() const { return Index; }

  /// Return the probe discriminator.
  ///
  /// \return Probe discriminator.
  uint32_t getDiscriminator() const { return Discriminator; }

  /// Return the probe attribute bitmask.
  ///
  /// \return Probe attribute bitmask.
  uint8_t getAttributes() const { return Attributes; }

  /// Return the probe type.
  ///
  /// \return Probe type.
  uint8_t getType() const { return Type; }

  /// Return true if this is a basic-block probe.
  ///
  /// \return True if this is a basic-block probe.
  bool isBlock() const {
    return Type == static_cast<uint8_t>(PseudoProbeType::Block);
  }

  /// Return true if this is an indirect-call probe.
  ///
  /// \return True if this is an indirect-call probe.
  bool isIndirectCall() const {
    return Type == static_cast<uint8_t>(PseudoProbeType::IndirectCall);
  }

  /// Return true if this is a direct-call probe.
  ///
  /// \return True if this is a direct-call probe.
  bool isDirectCall() const {
    return Type == static_cast<uint8_t>(PseudoProbeType::DirectCall);
  }

  /// Return true if this is any call probe.
  ///
  /// \return True if this is any call probe.
  bool isCall() const { return isIndirectCall() || isDirectCall(); }

  /// Replace the probe attribute bitmask with \p Attr.
  ///
  /// \param Attr - New attribute bitmask.
  void setAttributes(uint8_t Attr) { Attributes = Attr; }
};

/// A pseudo probe instance for one table entry.
///
/// Created while a machine instruction is assembled, using an address from a
/// temporary label at the current address in the current section.
class MCPseudoProbe : public MCPseudoProbeBase {
  uint64_t Guid;
  MCSymbol *Label;

public:
  /// Construct a probe bound to temporary label \p Label.
  ///
  /// \param Label - Symbol at the probe's code address.
  /// \param Guid - GUID of the originating function.
  /// \param Index - Probe index within that function.
  /// \param Type - Probe type.
  /// \param Attributes - Attribute bitmask.
  /// \param Discriminator - Discriminator value.
  MCPseudoProbe(MCSymbol *Label, uint64_t Guid, uint64_t Index, uint64_t Type,
                uint64_t Attributes, uint32_t Discriminator)
      : MCPseudoProbeBase(Index, Attributes, Type, Discriminator), Guid(Guid),
        Label(Label) {
    assert(Type <= 0xFF && "Probe type too big to encode, exceeding 2^8");
    assert(Attributes <= 0xFF &&
           "Probe attributes too big to encode, exceeding 2^16");
  }

  /// Return the GUID of the originating function.
  ///
  /// \return GUID of the originating function.
  uint64_t getGuid() const { return Guid; };
  /// Return the temporary label at the probe address.
  ///
  /// \return Temporary label at the probe address.
  MCSymbol *getLabel() const { return Label; }
  /// Emit this probe into \p MCOS relative to \p LastProbe.
  ///
  /// \param MCOS - Object streamer that receives the encoding.
  /// \param LastProbe - Previously emitted probe, or null for the first.
  LLVM_ABI void emit(MCObjectStreamer *MCOS,
                     const MCPseudoProbe *LastProbe) const;
};

/// A callsite identified by caller function name and probe id.
using MCPseudoProbeFrameLocation = std::pair<StringRef, uint32_t>;

/// A pseudo probe decoded from a `.pseudoprobe` section.
class MCDecodedPseudoProbe : public MCPseudoProbeBase {
  uint64_t Address;
  MCDecodedPseudoProbeInlineTree *InlineTree;

public:
  /// Construct a decoded probe at address \p Ad in inline tree \p Tree.
  ///
  /// \param Ad - Code address of the probe.
  /// \param I - Probe index.
  /// \param K - Probe type.
  /// \param At - Attribute bitmask.
  /// \param D - Discriminator value.
  /// \param Tree - Inline-tree node that owns this probe.
  MCDecodedPseudoProbe(uint64_t Ad, uint32_t I, PseudoProbeType K, uint8_t At,
                       uint32_t D, MCDecodedPseudoProbeInlineTree *Tree)
      : MCPseudoProbeBase(I, At, static_cast<uint8_t>(K), D), Address(Ad),
        InlineTree(Tree){};
  /// Return the GUID of the function that owns this probe.
  ///
  /// \return GUID of the function that owns this probe.
  LLVM_ABI uint64_t getGuid() const;

  /// Return the code address of this probe.
  ///
  /// \return Code address of this probe.
  uint64_t getAddress() const { return Address; }

  /// Set the code address of this probe to \p Addr.
  ///
  /// \param Addr - New code address.
  void setAddress(uint64_t Addr) { Address = Addr; }

  /// Return the inline-tree node that owns this probe.
  ///
  /// \return Inline-tree node that owns this probe.
  MCDecodedPseudoProbeInlineTree *getInlineTreeNode() const {
    return InlineTree;
  }

  /// Build the inlined context for this probe into \p ContextStack.
  ///
  /// Walks the inline tree backwards; each node's inline site becomes one
  /// frame. \p ContextStack is filled in root-to-leaf order.
  ///
  /// \param ContextStack - Output stack of frame locations.
  /// \param GUID2FuncMAP - GUID-to-function-descriptor map for names.
  LLVM_ABI void
  getInlineContext(SmallVectorImpl<MCPseudoProbeFrameLocation> &ContextStack,
                   const GUIDProbeFunctionMap &GUID2FuncMAP) const;

  /// Return the inlined context of this probe as a single string.
  ///
  /// \param GUID2FuncMAP - GUID-to-function-descriptor map for names.
  /// \return Formatted inline-context string.
  LLVM_ABI std::string
  getInlineContextStr(const GUIDProbeFunctionMap &GUID2FuncMAP) const;

  /// Print this probe for disassembly to \p OS.
  ///
  /// \param OS - Stream to print to.
  /// \param GUID2FuncMAP - GUID-to-function-descriptor map for names.
  /// \param ShowName - If true, include the function name.
  LLVM_ABI void print(raw_ostream &OS, const GUIDProbeFunctionMap &GUID2FuncMAP,
                      bool ShowName) const;
};

/// Sorted map from code address to decoded pseudo probes.
///
/// Inherits vector storage privately and re-exports the public
/// \c std::vector interface with documentation.
class AddressProbesMap
    : private std::vector<std::reference_wrapper<MCDecodedPseudoProbe>> {
  using Base = std::vector<std::reference_wrapper<MCDecodedPseudoProbe>>;

  auto getIt(uint64_t Addr) const {
    auto CompareProbe = [](const MCDecodedPseudoProbe &Probe, uint64_t Addr) {
      return Probe.getAddress() < Addr;
    };
    return llvm::lower_bound(*this, Addr, CompareProbe);
  }

public:
  /// Assign new contents, replacing existing probes.
  using Base::assign;
  /// Access the probe at index \p N with bounds checking.
  using Base::at;
  /// Return a reference to the last probe.
  using Base::back;
  /// Return an iterator to the first probe.
  using Base::begin;
  /// Return a pointer to the underlying probe storage.
  using Base::data;
  /// Return an iterator past the last probe.
  using Base::end;
  /// Erase probes from the map.
  using Base::erase;
  /// Return a reference to the first probe.
  using Base::front;
  /// Insert probes into the map.
  using Base::insert;
  /// Assign from another address-to-probes map.
  using Base::operator=;
  /// Access the probe at index \p N without bounds checking.
  using Base::operator[];
  /// Append a probe to the end.
  using Base::push_back;
  /// Return a reverse iterator to the last probe.
  using Base::rbegin;
  /// Return a reverse iterator past the first probe.
  using Base::rend;
  /// Resize the map to hold \p N probes.
  using Base::resize;
  /// Reserve storage for at least \p N probes.
  using Base::reserve;
  /// Append a probe constructed in place.
  using Base::emplace_back;
  /// Return the number of probes.
  using Base::size;
  /// Return whether the map has no probes.
  using Base::empty;

  /// Return the probes whose addresses lie in `[From, To)`.
  ///
  /// \param From - Inclusive start address.
  /// \param To - Exclusive end address.
  /// \return Iterator range over matching probes.
  auto find(uint64_t From, uint64_t To) const {
    return llvm::make_range(getIt(From), getIt(To));
  }
  /// Return the probes at exactly \p Address.
  ///
  /// \param Address - Code address to look up.
  /// \return Iterator range over matching probes, or empty if none.
  auto find(uint64_t Address) const {
    auto FromIt = getIt(Address);
    if (FromIt == end() || FromIt->get().getAddress() != Address)
      return llvm::make_range(end(), end());
    auto ToIt = getIt(Address + 1);
    return llvm::make_range(FromIt, ToIt);
  }
};

/// CRTP base for a trie that groups probes by inline stack.
template <typename ProbesType, typename DerivedProbeInlineTreeType,
          typename InlinedProbeTreeMap>
class MCPseudoProbeInlineTreeBase {
protected:
  /// Children of the current context (for example, inlinees).
  InlinedProbeTreeMap Children;
  /// Probes that originate in this function context.
  ProbesType Probes;
  /// Construct an empty node and check the CRTP base relationship.
  MCPseudoProbeInlineTreeBase() {
    static_assert(std::is_base_of<MCPseudoProbeInlineTreeBase,
                                  DerivedProbeInlineTreeType>::value,
                  "DerivedProbeInlineTreeType must be subclass of "
                  "MCPseudoProbeInlineTreeBase");
  }

public:
  /// GUID of the function represented by this node; zero for the root.
  uint64_t Guid = 0;

  /// Return true if this is the dummy root node (GUID 0).
  ///
  /// \return True if this is the dummy root node (GUID 0).
  bool isRoot() const { return Guid == 0; }
  /// Return the mutable map of child inline-tree nodes.
  ///
  /// \return Mutable map of child inline-tree nodes.
  InlinedProbeTreeMap &getChildren() { return Children; }
  /// Return the const map of child inline-tree nodes.
  ///
  /// \return Const map of child inline-tree nodes.
  const InlinedProbeTreeMap &getChildren() const { return Children; }
  /// Return the probes attached to this node.
  ///
  /// \return Probes attached to this node.
  const ProbesType &getProbes() const { return Probes; }
  /// Parent node of this inline site, or null for the root.
  MCPseudoProbeInlineTreeBase<ProbesType, DerivedProbeInlineTreeType,
                              InlinedProbeTreeMap> *Parent = nullptr;
  /// Return the child for \p Site, creating it if missing.
  ///
  /// \param Site - Inline site identifying the child.
  /// \return Pointer to the child node for \p Site.
  DerivedProbeInlineTreeType *getOrAddNode(const InlineSite &Site) {
    auto [It, Inserted] = Children.try_emplace(Site);
    if (Inserted) {
      It->second = std::make_unique<DerivedProbeInlineTreeType>(Site);
      It->second->Parent = this;
    }
    return It->second.get();
  };
};

/// Trie that groups probes by inline stack for one `.text` section.
///
/// A tree is allocated per standalone text section. A fake root instance is
/// created first; a real instance exists for each function, either a
/// non-inlined function with code in `.text` or an inlined function.
class MCPseudoProbeInlineTree
    : public MCPseudoProbeInlineTreeBase<
          std::vector<MCPseudoProbe>, MCPseudoProbeInlineTree,
          DenseMap<InlineSite, std::unique_ptr<MCPseudoProbeInlineTree>>> {
public:
  /// Construct an empty root node.
  MCPseudoProbeInlineTree() = default;
  /// Construct a node for the function with GUID \p Guid.
  ///
  /// \param Guid - Function GUID for this node.
  MCPseudoProbeInlineTree(uint64_t Guid) { this->Guid = Guid; }
  /// Construct a node for inline site \p Site.
  ///
  /// \param Site - Inline site whose callee GUID becomes this node's GUID.
  MCPseudoProbeInlineTree(const InlineSite &Site) {
    this->Guid = std::get<0>(Site);
  }

  /// Add \p Probe under the path described by \p InlineStack.
  ///
  /// \param Probe - Probe to insert.
  /// \param InlineStack - Inline path from outermost caller to the probe.
  LLVM_ABI void addPseudoProbe(const MCPseudoProbe &Probe,
                               const MCPseudoProbeInlineStack &InlineStack);
  /// Emit probes in this subtree to \p MCOS.
  ///
  /// \param MCOS - Object streamer that receives the encoding.
  /// \param LastProbe - In/out previous probe used for address deltas.
  LLVM_ABI void emit(MCObjectStreamer *MCOS, const MCPseudoProbe *&LastProbe);
};

/// Inline-tree node for a decoded pseudo probe.
class MCDecodedPseudoProbeInlineTree
    : public MCPseudoProbeInlineTreeBase<
          MCDecodedPseudoProbe *, MCDecodedPseudoProbeInlineTree,
          MutableArrayRef<MCDecodedPseudoProbeInlineTree>> {
  uint32_t NumProbes = 0;
  uint32_t ProbeId = 0;

public:
  /// Construct an empty root node.
  MCDecodedPseudoProbeInlineTree() = default;
  /// Construct a node for \p Site under \p Parent.
  ///
  /// \param Site - Inline site for this node.
  /// \param Parent - Parent node in the decoded inline tree.
  MCDecodedPseudoProbeInlineTree(const InlineSite &Site,
                                 MCDecodedPseudoProbeInlineTree *Parent)
      : ProbeId(std::get<1>(Site)) {
    this->Guid = std::get<0>(Site);
    this->Parent = Parent;
  }

  /// Return true if this node represents a real (non-dummy) inline site.
  ///
  /// \return True if this node represents a real (non-dummy) inline site.
  bool hasInlineSite() const { return !isRoot() && !Parent->isRoot(); }
  /// Return true if this node is an outlined top-level function.
  ///
  /// \return True if this node is an outlined top-level function.
  bool isTopLevelFunc() const { return !isRoot() && Parent->isRoot(); }
  /// Return the inline site identifying this node.
  ///
  /// \return Inline site identifying this node.
  InlineSite getInlineSite() const { return InlineSite(Guid, ProbeId); }
  /// Bind this node to the contiguous probe range \p ProbesRef.
  ///
  /// \param ProbesRef - Probes owned by this node.
  void setProbes(MutableArrayRef<MCDecodedPseudoProbe> ProbesRef) {
    Probes = ProbesRef.data();
    NumProbes = ProbesRef.size();
  }
  /// Return the probes owned by this node.
  ///
  /// \return Probes owned by this node.
  auto getProbes() const {
    return MutableArrayRef<MCDecodedPseudoProbe>(Probes, NumProbes);
  }
};

/// Pseudo probes collected for one compile unit, grouped by function.
class MCPseudoProbeSections {
public:
  /// Add \p Probe for function \p FuncSym under \p InlineStack.
  ///
  /// \param FuncSym - Symbol of the containing function.
  /// \param Probe - Probe to insert.
  /// \param InlineStack - Inline path from outermost caller to the probe.
  void addPseudoProbe(MCSymbol *FuncSym, const MCPseudoProbe &Probe,
                      const MCPseudoProbeInlineStack &InlineStack) {
    MCProbeDivisions[FuncSym].addPseudoProbe(Probe, InlineStack);
  }

  /// Map from function symbol to its probe inline tree.
  ///
  /// Addresses of `MCPseudoProbeInlineTree` are used by the tree and must stay
  /// stable.
  using MCProbeDivisionMap = std::unordered_map<MCSymbol *, MCPseudoProbeInlineTree>;

private:
  // A collection of MCPseudoProbe for each function. The MCPseudoProbes are
  // grouped by GUIDs due to inlining that can bring probes from different
  // functions into one function.
  MCProbeDivisionMap MCProbeDivisions;

public:
  /// Return the map of per-function probe trees.
  ///
  /// \return Map of per-function probe trees.
  const MCProbeDivisionMap &getMCProbes() const { return MCProbeDivisions; }

  /// Return true if no probes have been collected.
  ///
  /// \return True if no probes have been collected.
  bool empty() const { return MCProbeDivisions.empty(); }

  /// Emit all collected probes into \p MCOS.
  ///
  /// \param MCOS - Object streamer that receives the encoding.
  LLVM_ABI void emit(MCObjectStreamer *MCOS);
};

/// Module-level holder for pseudo probes to emit into `.pseudoprobe` sections.
class MCPseudoProbeTable {
  // A collection of MCPseudoProbe in the current module grouped by
  // functions. MCPseudoProbes will be encoded into a corresponding
  // .pseudoprobe section. With functions emitted as separate comdats,
  // a text section really only contains the code of a function solely, and the
  // probes associated with the text section will be emitted into a standalone
  // .pseudoprobe section that shares the same comdat group with the function.
  MCPseudoProbeSections MCProbeSections;

public:
  /// Emit the streamer's pending pseudo probes through \p MCOS.
  ///
  /// \param MCOS - Object streamer that owns the probe table.
  LLVM_ABI static void emit(MCObjectStreamer *MCOS);

  /// Return the mutable collection of per-function probe sections.
  ///
  /// \return Mutable collection of per-function probe sections.
  MCPseudoProbeSections &getProbeSections() { return MCProbeSections; }

#ifndef NDEBUG
  /// Indentation depth used when dumping probe debug graphs.
  static int DdgPrintIndent;
#endif
};

/// Decoder for `.pseudo_probe_desc` and `.pseudoprobe` section contents.
class MCPseudoProbeDecoder {
  // Decoded pseudo probes vector.
  std::vector<MCDecodedPseudoProbe> PseudoProbeVec;
  // Injected pseudo probes, identified by the containing inline tree node.
  // Need to keep injected probes separately for two reasons:
  // 1) Probes cannot be added to the PseudoProbeVec: appending may cause
  //    reallocation so that pointers to its elements will become invalid.
  // 2) Probes belonging to function record must be contiguous in PseudoProbeVec
  //    as owning InlineTree references them with an ArrayRef to save space.
  DenseMap<const MCDecodedPseudoProbeInlineTree *,
           std::vector<MCDecodedPseudoProbe>>
      InjectedProbeMap;
  // Decoded inline records vector.
  std::vector<MCDecodedPseudoProbeInlineTree> InlineTreeVec;

  // GUID to PseudoProbeFuncDesc map.
  GUIDProbeFunctionMap GUID2FuncDescMap;

  BumpPtrAllocator FuncNameAllocator;

  // Address to probes map.
  AddressProbesMap Address2ProbesMap;

  // The dummy root of the inline trie, all the outlined function will directly
  // be the children of the dummy root, all the inlined function will be the
  // children of its inlineer. So the relation would be like:
  // DummyRoot --> OutlinedFunc --> InlinedFunc1 --> InlinedFunc2
  MCDecodedPseudoProbeInlineTree DummyInlineRoot;

  /// Points to the current location in the buffer.
  const uint8_t *Data = nullptr;

  /// Points to the end of the buffer.
  const uint8_t *End = nullptr;

  /// Whether encoding is based on a starting probe with absolute code address.
  bool EncodingIsAddrBased = false;

  // Decoding helper function
  template <typename T> ErrorOr<T> readUnencodedNumber();
  template <typename T> ErrorOr<T> readUnsignedNumber();
  template <typename T> ErrorOr<T> readSignedNumber();
  ErrorOr<StringRef> readString(uint32_t Size);

public:
  // MCPseudoProbeDecoder cannot be copied/moved due to address dependence on
  // the DummyInlineRoot member address.
  /// Construct an empty decoder.
  MCPseudoProbeDecoder() = default;
  /// Deleted copy constructor; the decoder depends on `DummyInlineRoot`'s address.
  ///
  /// \param Other - Unused; copy construction is deleted.
  MCPseudoProbeDecoder(const MCPseudoProbeDecoder &Other) = delete;
  /// Deleted move constructor; the decoder depends on `DummyInlineRoot`'s address.
  ///
  /// \param Other - Unused; move construction is deleted.
  MCPseudoProbeDecoder(MCPseudoProbeDecoder &&Other) = delete;
  /// Deleted copy assignment; the decoder depends on `DummyInlineRoot`'s address.
  ///
  /// \param Other - Unused; copy assignment is deleted.
  MCPseudoProbeDecoder &operator=(const MCPseudoProbeDecoder &Other) = delete;
  /// Deleted move assignment; the decoder depends on `DummyInlineRoot`'s address.
  ///
  /// \param Other - Unused; move assignment is deleted.
  MCPseudoProbeDecoder &operator=(MCPseudoProbeDecoder &&Other) = delete;
  /// Destroy the decoder and release decoded probe storage.
  ~MCPseudoProbeDecoder() = default;

  /// Set of 64-bit GUIDs or addresses used as a filter.
  using Uint64Set = DenseSet<uint64_t>;
  /// Map from 64-bit key to 64-bit value (for example, GUID to start address).
  using Uint64Map = DenseMap<uint64_t, uint64_t>;

  /// Decode a `.pseudo_probe_desc` section into the GUID-to-descriptor map.
  ///
  /// When the section is memory-mapped and \p IsMMapped is true, string names
  /// may reference the mapped bytes directly.
  ///
  /// \param Start - Start of the section bytes.
  /// \param Size - Size of the section in bytes.
  /// \param IsMMapped - True if \p Start points into a mapped section.
  /// \param VerboseWarnings - Emit detailed warnings on decode issues.
  /// \return True on success.
  LLVM_ABI bool buildGUID2FuncDescMap(const uint8_t *Start, std::size_t Size,
                                      bool IsMMapped = false,
                                      bool VerboseWarnings = false);

  /// Count probes and inlined records while decoding a function body.
  ///
  /// \param Discard - Set to true when the current function should be skipped.
  /// \param ProbeCount - Receives the number of probes in the function.
  /// \param InlinedCount - Receives the number of first-level inlinees.
  /// \param GuidFilter - When non-empty, only count matching function GUIDs.
  /// \return True on success.
  template <bool IsTopLevelFunc>
  bool countRecords(bool &Discard, uint32_t &ProbeCount, uint32_t &InlinedCount,
                    const Uint64Set &GuidFilter);

  /// Decode a `.pseudoprobe` section into the address-to-probes map.
  ///
  /// Only functions selected by \p GuildFilter are retained.
  ///
  /// \param Start - Start of the section bytes.
  /// \param Size - Size of the section in bytes.
  /// \param GuildFilter - GUIDs of functions to decode; empty means all.
  /// \param FuncStartAddrs - Map from GUID to function start address.
  /// \return True on success.
  LLVM_ABI bool buildAddress2ProbeMap(const uint8_t *Start, std::size_t Size,
                                      const Uint64Set &GuildFilter,
                                      const Uint64Map &FuncStartAddrs);

  /// Print the decoded GUID-to-function-descriptor map to \p OS.
  ///
  /// \param OS - Stream to print to.
  LLVM_ABI void printGUID2FuncDescMap(raw_ostream &OS);

  /// Print probes at \p Address to \p OS (for use with disassembly).
  ///
  /// \param OS - Stream to print to.
  /// \param Address - Code address whose probes should be printed.
  LLVM_ABI void printProbeForAddress(raw_ostream &OS, uint64_t Address);

  /// Print probes for every known address to \p OS.
  ///
  /// \param OS - Stream to print to.
  LLVM_ABI void printProbesForAllAddresses(raw_ostream &OS);

  /// Return the call probe at \p Address, if any.
  ///
  /// \param Address - Code address to look up.
  /// \return Matching call probe, or null if none.
  LLVM_ABI const MCDecodedPseudoProbe *
  getCallProbeForAddr(uint64_t Address) const;

  /// Return the function descriptor for \p GUID, if present.
  ///
  /// \param GUID - Function GUID to look up.
  /// \return Matching descriptor, or null if none.
  LLVM_ABI const MCPseudoProbeFuncDesc *getFuncDescForGUID(uint64_t GUID) const;

  /// Populate \p InlineContextStack with the inline stack of \p Probe.
  ///
  /// When \p IncludeLeaf is true, the probe's own location is appended.
  /// Example for probe `bar:3` inlined at `foo:2` then `main:1`:
  /// - IncludeLeaf = true  → `[main:1, foo:2, bar:3]`
  /// - IncludeLeaf = false → `[main:1, foo:2]`
  ///
  /// \param Probe - Probe whose inline context is requested.
  /// \param InlineContextStack - Output stack of frame locations.
  /// \param IncludeLeaf - Include the probe's own location when true.
  LLVM_ABI void getInlineContextForProbe(
      const MCDecodedPseudoProbe *Probe,
      SmallVectorImpl<MCPseudoProbeFrameLocation> &InlineContextStack,
      bool IncludeLeaf) const;

  /// Return the const address-to-probes map.
  ///
  /// \return Const address-to-probes map.
  const AddressProbesMap &getAddress2ProbesMap() const {
    return Address2ProbesMap;
  }

  /// Return the mutable address-to-probes map.
  ///
  /// \return Mutable address-to-probes map.
  AddressProbesMap &getAddress2ProbesMap() { return Address2ProbesMap; }

  /// Return the GUID-to-function-descriptor map.
  ///
  /// \return GUID-to-function-descriptor map.
  const GUIDProbeFunctionMap &getGUID2FuncDescMap() const {
    return GUID2FuncDescMap;
  }

  /// Return the descriptor of the function that inlined \p Probe.
  ///
  /// \param Probe - Probe whose inliner is requested.
  /// \return Inliner descriptor, or null if none.
  LLVM_ABI const MCPseudoProbeFuncDesc *
  getInlinerDescForProbe(const MCDecodedPseudoProbe *Probe) const;

  /// Return the dummy root of the decoded inline trie.
  ///
  /// \return Dummy root of the decoded inline trie.
  const MCDecodedPseudoProbeInlineTree &getDummyInlineRoot() const {
    return DummyInlineRoot;
  }

  /// Inject a copy of \p Probe at \p Address under its inline-tree parent.
  ///
  /// \param Probe - Probe to inject.
  /// \param Address - Code address assigned to the injected probe.
  void addInjectedProbe(const MCDecodedPseudoProbe &Probe, uint64_t Address) {
    const MCDecodedPseudoProbeInlineTree *Parent = Probe.getInlineTreeNode();
    InjectedProbeMap[Parent].emplace_back(Probe).setAddress(Address);
  }

  /// Return how many injected probes belong to \p Parent.
  ///
  /// \param Parent - Inline-tree node that owns the injected probes.
  /// \return Number of injected probes, or zero if none.
  size_t
  getNumInjectedProbes(const MCDecodedPseudoProbeInlineTree *Parent) const {
    auto It = InjectedProbeMap.find(Parent);
    if (It == InjectedProbeMap.end())
      return 0;
    return It->second.size();
  }

  /// Return the injected probes belonging to \p Parent.
  ///
  /// \param Parent - Inline-tree node that owns the injected probes.
  /// \return Iterator range over injected probes for \p Parent.
  auto getInjectedProbes(MCDecodedPseudoProbeInlineTree *Parent) {
    auto It = InjectedProbeMap.find(Parent);
    assert(It != InjectedProbeMap.end());
    return iterator_range(It->second);
  }

  /// Return the flat vector of decoded inline-tree nodes.
  ///
  /// \return Flat vector of decoded inline-tree nodes.
  ArrayRef<MCDecodedPseudoProbeInlineTree> getInlineTreeVec() const {
    return InlineTreeVec;
  }

private:
  // Recursively parse an inlining tree encoded in pseudo_probe section. Returns
  // whether the the top-level node should be skipped.
  template <bool IsTopLevelFunc>
  bool buildAddress2ProbeMap(MCDecodedPseudoProbeInlineTree *Cur,
                             uint64_t &LastAddr, const Uint64Set &GuildFilter,
                             const Uint64Map &FuncStartAddrs,
                             const uint32_t CurChildIndex);
};

} // end namespace llvm

#endif // LLVM_MC_MCPSEUDOPROBE_H
