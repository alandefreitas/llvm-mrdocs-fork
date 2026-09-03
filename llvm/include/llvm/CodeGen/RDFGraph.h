//===- RDFGraph.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Target-independent, SSA-based data flow graph for register data flow (RDF)
// for a non-SSA program representation (e.g. post-RA machine code).
//
//
// *** Introduction
//
// The RDF graph is a collection of nodes, each of which denotes some element
// of the program. There are two main types of such elements: code and refe-
// rences. Conceptually, "code" is something that represents the structure
// of the program, e.g. basic block or a statement, while "reference" is an
// instance of accessing a register, e.g. a definition or a use. Nodes are
// connected with each other based on the structure of the program (such as
// blocks, instructions, etc.), and based on the data flow (e.g. reaching
// definitions, reached uses, etc.). The single-reaching-definition principle
// of SSA is generally observed, although, due to the non-SSA representation
// of the program, there are some differences between the graph and a "pure"
// SSA representation.
//
//
// *** Implementation remarks
//
// Since the graph can contain a large number of nodes, memory consumption
// was one of the major design considerations. As a result, there is a single
// base class NodeBase which defines all members used by all possible derived
// classes. The members are arranged in a union, and a derived class cannot
// add any data members of its own. Each derived class only defines the
// functional interface, i.e. member functions. NodeBase must be a POD,
// which implies that all of its members must also be PODs.
// Since nodes need to be connected with other nodes, pointers have been
// replaced with 32-bit identifiers: each node has an id of type NodeId.
// There are mapping functions in the graph that translate between actual
// memory addresses and the corresponding identifiers.
// A node id of 0 is equivalent to nullptr.
//
//
// *** Structure of the graph
//
// A code node is always a collection of other nodes. For example, a code
// node corresponding to a basic block will contain code nodes corresponding
// to instructions. In turn, a code node corresponding to an instruction will
// contain a list of reference nodes that correspond to the definitions and
// uses of registers in that instruction. The members are arranged into a
// circular list, which is yet another consequence of the effort to save
// memory: for each member node it should be possible to obtain its owner,
// and it should be possible to access all other members. There are other
// ways to accomplish that, but the circular list seemed the most natural.
//
// +- CodeNode -+
// |            | <---------------------------------------------------+
// +-+--------+-+                                                     |
//   |FirstM  |LastM                                                  |
//   |        +-------------------------------------+                 |
//   |                                              |                 |
//   V                                              V                 |
//  +----------+ Next +----------+ Next       Next +----------+ Next  |
//  |          |----->|          |-----> ... ----->|          |----->-+
//  +- Member -+      +- Member -+                 +- Member -+
//
// The order of members is such that related reference nodes (see below)
// should be contiguous on the member list.
//
// A reference node is a node that encapsulates an access to a register,
// in other words, data flowing into or out of a register. There are two
// major kinds of reference nodes: defs and uses. A def node will contain
// the id of the first reached use, and the id of the first reached def.
// Each def and use will contain the id of the reaching def, and also the
// id of the next reached def (for def nodes) or use (for use nodes).
// The "next node sharing the same reaching def" is denoted as "sibling".
// In summary:
// - Def node contains: reaching def, sibling, first reached def, and first
// reached use.
// - Use node contains: reaching def and sibling.
//
// +-- DefNode --+
// | R2 = ...    | <---+--------------------+
// ++---------+--+     |                    |
//  |Reached  |Reached |                    |
//  |Def      |Use     |                    |
//  |         |        |Reaching            |Reaching
//  |         V        |Def                 |Def
//  |      +-- UseNode --+ Sib  +-- UseNode --+ Sib       Sib
//  |      | ... = R2    |----->| ... = R2    |----> ... ----> 0
//  |      +-------------+      +-------------+
//  V
// +-- DefNode --+ Sib
// | R2 = ...    |----> ...
// ++---------+--+
//  |         |
//  |         |
// ...       ...
//
// To get a full picture, the circular lists connecting blocks within a
// function, instructions within a block, etc. should be superimposed with
// the def-def, def-use links shown above.
// To illustrate this, consider a small example in a pseudo-assembly:
// foo:
//   add r2, r0, r1   ; r2 = r0+r1
//   addi r0, r2, 1   ; r0 = r2+1
//   ret r0           ; return value in r0
//
// The graph (in a format used by the debugging functions) would look like:
//
//   DFG dump:[
//   f1: Function foo
//   b2: === %bb.0 === preds(0), succs(0):
//   p3: phi [d4<r0>(,d12,u9):]
//   p5: phi [d6<r1>(,,u10):]
//   s7: add [d8<r2>(,,u13):, u9<r0>(d4):, u10<r1>(d6):]
//   s11: addi [d12<r0>(d4,,u15):, u13<r2>(d8):]
//   s14: ret [u15<r0>(d12):]
//   ]
//
// The f1, b2, p3, etc. are node ids. The letter is prepended to indicate the
// kind of the node (i.e. f - function, b - basic block, p - phi, s - state-
// ment, d - def, u - use).
// The format of a def node is:
//   dN<R>(rd,d,u):sib,
// where
//   N   - numeric node id,
//   R   - register being defined
//   rd  - reaching def,
//   d   - reached def,
//   u   - reached use,
//   sib - sibling.
// The format of a use node is:
//   uN<R>[!](rd):sib,
// where
//   N   - numeric node id,
//   R   - register being used,
//   rd  - reaching def,
//   sib - sibling.
// Possible annotations (usually preceding the node id):
//   +   - preserving def,
//   ~   - clobbering def,
//   "   - shadow ref (follows the node id),
//   !   - fixed register (appears after register name).
//
// The circular lists are not explicit in the dump.
//
//
// *** Node attributes
//
// NodeBase has a member "Attrs", which is the primary way of determining
// the node's characteristics. The fields in this member decide whether
// the node is a code node or a reference node (i.e. node's "type"), then
// within each type, the "kind" determines what specifically this node
// represents. The remaining bits, "flags", contain additional information
// that is even more detailed than the "kind".
// CodeNode's kinds are:
// - Phi:   Phi node, members are reference nodes.
// - Stmt:  Statement, members are reference nodes.
// - Block: Basic block, members are instruction nodes (i.e. Phi or Stmt).
// - Func:  The whole function. The members are basic block nodes.
// RefNode's kinds are:
// - Use.
// - Def.
//
// Meaning of flags:
// - Preserving: applies only to defs. A preserving def is one that can
//   preserve some of the original bits among those that are included in
//   the register associated with that def. For example, if R0 is a 32-bit
//   register, but a def can only change the lower 16 bits, then it will
//   be marked as preserving.
// - Shadow: a reference that has duplicates holding additional reaching
//   defs (see more below).
// - Clobbering: applied only to defs, indicates that the value generated
//   by this def is unspecified. A typical example would be volatile registers
//   after function calls.
// - Fixed: the register in this def/use cannot be replaced with any other
//   register. A typical case would be a parameter register to a call, or
//   the register with the return value from a function.
// - Undef: the register in this reference the register is assumed to have
//   no pre-existing value, even if it appears to be reached by some def.
//   This is typically used to prevent keeping registers artificially live
//   in cases when they are defined via predicated instructions. For example:
//     r0 = add-if-true cond, r10, r11                (1)
//     r0 = add-if-false cond, r12, r13, implicit r0  (2)
//     ... = r0                                       (3)
//   Before (1), r0 is not intended to be live, and the use of r0 in (3) is
//   not meant to be reached by any def preceding (1). However, since the
//   defs in (1) and (2) are both preserving, these properties alone would
//   imply that the use in (3) may indeed be reached by some prior def.
//   Adding Undef flag to the def in (1) prevents that. The Undef flag
//   may be applied to both defs and uses.
// - Dead: applies only to defs. The value coming out of a "dead" def is
//   assumed to be unused, even if the def appears to be reaching other defs
//   or uses. The motivation for this flag comes from dead defs on function
//   calls: there is no way to determine if such a def is dead without
//   analyzing the target's ABI. Hence the graph should contain this info,
//   as it is unavailable otherwise. On the other hand, a def without any
//   uses on a typical instruction is not the intended target for this flag.
//
// *** Shadow references
//
// It may happen that a super-register can have two (or more) non-overlapping
// sub-registers. When both of these sub-registers are defined and followed
// by a use of the super-register, the use of the super-register will not
// have a unique reaching def: both defs of the sub-registers need to be
// accounted for. In such cases, a duplicate use of the super-register is
// added and it points to the extra reaching def. Both uses are marked with
// a flag "shadow". Example:
// Assume t0 is a super-register of r0 and r1, r0 and r1 do not overlap:
//   set r0, 1        ; r0 = 1
//   set r1, 1        ; r1 = 1
//   addi t1, t0, 1   ; t1 = t0+1
//
// The DFG:
//   s1: set [d2<r0>(,,u9):]
//   s3: set [d4<r1>(,,u10):]
//   s5: addi [d6<t1>(,,):, u7"<t0>(d2):, u8"<t0>(d4):]
//
// The statement s5 has two use nodes for t0: u7" and u9". The quotation
// mark " indicates that the node is a shadow.
//

#ifndef LLVM_CODEGEN_RDFGRAPH_H
#define LLVM_CODEGEN_RDFGRAPH_H

#include "RDFRegisters.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

// RDF uses uint32_t to refer to registers. This is to ensure that the type
// size remains specific. In other places, registers are often stored using
// unsigned.
static_assert(sizeof(uint32_t) == sizeof(unsigned), "Those should be equal");

namespace llvm {

class MachineBasicBlock;
class MachineDominanceFrontier;
class MachineDominatorTree;
class MachineFunction;
class MachineInstr;
class MachineOperand;
class raw_ostream;
class TargetInstrInfo;
class TargetRegisterInfo;

/// Register data-flow (RDF) graph types and builders for non-SSA machine code.
namespace rdf {

/// Opaque 32-bit identifier for a node in a data-flow graph.
using NodeId = uint32_t;

struct DataFlowGraph;

/// Packed attribute bits describing a node's type, kind, and flags.
struct NodeAttrs {
  // clang-format off
  /// Bit-field constants for node type, kind, and flag attributes.
  enum : uint16_t {
    /// No attributes set.
    None          = 0x0000,

    // Types: 2 bits
    /// Mask selecting the type field.
    TypeMask      = 0x0003,
    /// Code (container) node type.
    Code          = 0x0001,
    /// Reference (def/use) node type.
    Ref           = 0x0002,

    // Kind: 3 bits
    /// Mask selecting the kind field.
    KindMask      = 0x0007 << 2,
    /// Definition reference kind.
    Def           = 0x0001 << 2,
    /// Use reference kind.
    Use           = 0x0002 << 2,
    /// Phi instruction kind.
    Phi           = 0x0003 << 2,
    /// Statement (non-phi instruction) kind.
    Stmt          = 0x0004 << 2,
    /// Basic-block kind.
    Block         = 0x0005 << 2,
    /// Function kind.
    Func          = 0x0006 << 2,

    // Flags: 7 bits for now
    /// Mask selecting the flags field.
    FlagMask      = 0x007F << 5,
    /// Reference that has duplicate shadow refs for extra reaching defs.
    Shadow        = 0x0001 << 5,
    /// Def that produces unspecified (clobbering) values.
    Clobbering    = 0x0002 << 5,
    /// Reference that is a member of a phi node.
    PhiRef        = 0x0004 << 5,
    /// Def that can preserve some original register bits.
    Preserving    = 0x0008 << 5,
    /// Reference whose register cannot be replaced.
    Fixed         = 0x0010 << 5,
    /// Reference assumed to have no pre-existing value.
    Undef         = 0x0020 << 5,
    /// Def whose outgoing value is assumed unused.
    Dead          = 0x0040 << 5,
  };
  // clang-format on

  /// Extract the type bits from attribute word \p T.
  /// \param T Packed attribute word.
  /// \return The type field extracted from \p T.
  static uint16_t type(uint16_t T) { //
    return T & TypeMask;
  }
  /// Extract the kind bits from attribute word \p T.
  /// \param T Packed attribute word.
  /// \return The kind field extracted from \p T.
  static uint16_t kind(uint16_t T) { //
    return T & KindMask;
  }
  /// Extract the flag bits from attribute word \p T.
  /// \param T Packed attribute word.
  /// \return The flag field extracted from \p T.
  static uint16_t flags(uint16_t T) { //
    return T & FlagMask;
  }
  /// Return attribute word \p A with its type bits replaced by \p T.
  /// \param A Existing packed attribute word.
  /// \param T New type bits to install.
  /// \return \p A with its type bits set to \p T.
  static uint16_t set_type(uint16_t A, uint16_t T) {
    return (A & ~TypeMask) | T;
  }

  /// Return attribute word \p A with its kind bits replaced by \p K.
  /// \param A Existing packed attribute word.
  /// \param K New kind bits to install.
  /// \return \p A with its kind bits set to \p K.
  static uint16_t set_kind(uint16_t A, uint16_t K) {
    return (A & ~KindMask) | K;
  }

  /// Return attribute word \p A with its flag bits replaced by \p F.
  /// \param A Existing packed attribute word.
  /// \param F New flag bits to install.
  /// \return \p A with its flag bits set to \p F.
  static uint16_t set_flags(uint16_t A, uint16_t F) {
    return (A & ~FlagMask) | F;
  }

  /// Return true if code node attributes \p A may contain a member with \p B.
  /// \param A Attributes of a candidate owner (code) node.
  /// \param B Attributes of a candidate member node.
  /// \return True when a code node with \p A may own a member with \p B.
  static bool contains(uint16_t A, uint16_t B) {
    if (type(A) != Code)
      return false;
    uint16_t KB = kind(B);
    switch (kind(A)) {
    case Func:
      return KB == Block;
    case Block:
      return KB == Phi || KB == Stmt;
    case Phi:
    case Stmt:
      return type(B) == Ref;
    }
    return false;
  }
};

/// Flags controlling how a data-flow graph is built.
struct BuildOptions {
  /// Bit flags passed to \c DataFlowGraph::build.
  enum : unsigned {
    /// No special build options.
    None = 0x00,
    /// Do not remove dead phis during build.
    KeepDeadPhis = 0x01,
    /// Do not track reserved registers.
    OmitReserved = 0x02,
  };
};

/// Pair of a typed node pointer and its \c NodeId.
///
/// Used instead of \c std::pair so nodes can be cast between related
/// address types while keeping id and pointer in sync.
template <typename T> struct NodeAddr {
  /// Construct a null node address with id 0.
  NodeAddr() = default;
  /// Construct from pointer \p A and id \p I.
  /// \param A Node pointer.
  /// \param I Corresponding node id.
  NodeAddr(T A, NodeId I) : Addr(A), Id(I) {}

  /// Casting constructor from a related \c NodeAddr type.
  /// \param NA Source address whose pointer is statically cast to \p T.
  template <typename S>
  NodeAddr(const NodeAddr<S> &NA) : Addr(static_cast<T>(NA.Addr)), Id(NA.Id) {}

  /// Return true if this address equals \p NA (pointer and id agree).
  /// \param NA Other node address to compare.
  /// \return True when pointer and id both match \p NA.
  bool operator==(const NodeAddr<T> &NA) const {
    assert((Addr == NA.Addr) == (Id == NA.Id));
    return Addr == NA.Addr;
  }
  /// Return true if this address differs from \p NA.
  /// \param NA Other node address to compare.
  /// \return True when this address is not equal to \p NA.
  bool operator!=(const NodeAddr<T> &NA) const { //
    return !operator==(NA);
  }

  /// Typed pointer to the node object, or nullptr.
  T Addr = nullptr;
  /// Identifier of the node in the owning graph, or 0 for null.
  NodeId Id = 0;
};

struct NodeBase;

struct RefNode;
struct DefNode;
struct UseNode;
struct PhiUseNode;

struct CodeNode;
struct InstrNode;
struct PhiNode;
struct StmtNode;
struct BlockNode;
struct FuncNode;

// Use these short names with rdf:: qualification to avoid conflicts with
// preexisting names. Do not use 'using namespace rdf'.
/// Node address pointing at a generic \c NodeBase.
using Node = NodeAddr<NodeBase *>;

/// Node address pointing at a \c RefNode.
using Ref = NodeAddr<RefNode *>;
/// Node address pointing at a \c DefNode.
using Def = NodeAddr<DefNode *>;
/// Node address pointing at a \c UseNode.
using Use = NodeAddr<UseNode *>; // This may conflict with llvm::Use.
/// Node address pointing at a \c PhiUseNode.
using PhiUse = NodeAddr<PhiUseNode *>;

/// Node address pointing at a \c CodeNode.
using Code = NodeAddr<CodeNode *>;
/// Node address pointing at an \c InstrNode.
using Instr = NodeAddr<InstrNode *>;
/// Node address pointing at a \c PhiNode.
using Phi = NodeAddr<PhiNode *>;
/// Node address pointing at a \c StmtNode.
using Stmt = NodeAddr<StmtNode *>;
/// Node address pointing at a \c BlockNode.
using Block = NodeAddr<BlockNode *>;
/// Node address pointing at a \c FuncNode.
using Func = NodeAddr<FuncNode *>;

/// Bump-pointer allocator that maps between node ids and addresses.
///
/// Fast memory allocation and translation between node id and node address.
/// This is really the same idea as the one underlying the "bump pointer
/// allocator", the difference being in the translation. A node id is
/// composed of two components: the index of the block in which it was
/// allocated, and the index within the block. With the default settings,
/// where the number of nodes per block is 4096, the node id (minus 1) is:
///
/// bit position:                11             0
/// +----------------------------+--------------+
/// | Index of the block         |Index in block|
/// +----------------------------+--------------+
///
/// The actual node id is the above plus 1, to avoid creating a node id of 0.
///
/// This method significantly improved the build time, compared to using maps
/// (std::unordered_map or DenseMap) to translate between pointers and ids.
struct NodeAllocator {
  /// Constants controlling per-node storage layout.
  enum {
    /// Storage size in bytes reserved for each allocated node.
    NodeMemSize = 32
  };

  /// Construct an allocator that packs \p NPB nodes per storage block.
  /// \param NPB Nodes per block; must be a power of two (default 4096).
  NodeAllocator(uint32_t NPB = 4096)
      : NodesPerBlock(NPB), BitsPerIndex(Log2_32(NPB)),
        IndexMask((1 << BitsPerIndex) - 1) {
    assert(isPowerOf2_32(NPB));
  }

  /// Return the node pointer for id \p N.
  /// \param N Non-zero node id.
  /// \return Pointer to the node stored under \p N.
  NodeBase *ptr(NodeId N) const {
    uint32_t N1 = N - 1;
    uint32_t BlockN = N1 >> BitsPerIndex;
    uint32_t Offset = (N1 & IndexMask) * NodeMemSize;
    return reinterpret_cast<NodeBase *>(Blocks[BlockN] + Offset);
  }

  /// Return the node id corresponding to pointer \p P.
  /// \param P Address of a node allocated by this allocator.
  /// \return Node id for \p P.
  LLVM_ABI NodeId id(const NodeBase *P) const;
  /// Allocate and return a new zero-initialized node.
  /// \return Address of the newly allocated node.
  LLVM_ABI Node New();
  /// Release all allocated nodes and reset the allocator.
  LLVM_ABI void clear();

private:
  void startNewBlock();
  bool needNewBlock();

  uint32_t makeId(uint32_t Block, uint32_t Index) const {
    // Add 1 to the id, to avoid the id of 0, which is treated as "null".
    return ((Block << BitsPerIndex) | Index) + 1;
  }

  const uint32_t NodesPerBlock;
  const uint32_t BitsPerIndex;
  const uint32_t IndexMask;
  char *ActiveEnd = nullptr;
  std::vector<char *> Blocks;
  using AllocatorTy = BumpPtrAllocatorImpl<MallocAllocator, 65536>;
  AllocatorTy MemPool;
};

/// Ordered set of register references.
using RegisterSet = std::set<RegisterRef, RegisterRefLess>;

/// Target hooks classifying instruction operands for RDF construction.
struct LLVM_ABI TargetOperandInfo {
  /// Construct with target instruction info \p tii.
  /// \param tii Target instruction info used by the classifiers.
  TargetOperandInfo(const TargetInstrInfo &tii) : TII(tii) {}
  /// Destroy this operand-info object.
  virtual ~TargetOperandInfo() = default;

  /// Return true if operand \p OpNum of \p In is a preserving def.
  /// \param In Instruction being examined.
  /// \param OpNum Operand index within \p In.
  /// \return True when the operand is a preserving definition.
  virtual bool isPreserving(const MachineInstr &In, unsigned OpNum) const;
  /// Return true if operand \p OpNum of \p In is a clobbering def.
  /// \param In Instruction being examined.
  /// \param OpNum Operand index within \p In.
  /// \return True when the operand is a clobbering definition.
  virtual bool isClobbering(const MachineInstr &In, unsigned OpNum) const;
  /// Return true if operand \p OpNum of \p In is a fixed register.
  /// \param In Instruction being examined.
  /// \param OpNum Operand index within \p In.
  /// \return True when the operand uses a fixed register.
  virtual bool isFixedReg(const MachineInstr &In, unsigned OpNum) const;

  /// Target instruction info used by the virtual classifiers.
  const TargetInstrInfo &TII;
};

/// Packed register reference used only for compact storage.
struct PackedRegisterRef {
  /// Register identifier.
  RegisterId Id;
  /// Index of the lane mask in a \c LaneMaskIndex table.
  uint32_t MaskId;
};

/// Indexed set mapping lane masks to compact indices for packing.
struct LaneMaskIndex : private IndexedSet<LaneBitmask> {
  /// Construct an empty lane-mask index.
  LaneMaskIndex() = default;

  /// Return the lane mask stored under index \p K (0 means all lanes).
  /// \param K Lane-mask index previously returned by \c getIndexForLaneMask.
  /// \return Lane mask for \p K, or all-lanes when \p K is 0.
  LaneBitmask getLaneMaskForIndex(uint32_t K) const {
    return K == 0 ? LaneBitmask::getAll() : get(K);
  }

  /// Insert \p LM if needed and return its compact index (0 for all-lanes).
  /// \param LM Non-empty lane mask to index.
  /// \return Compact index for \p LM (0 when \p LM covers all lanes).
  uint32_t getIndexForLaneMask(LaneBitmask LM) {
    assert(LM.any());
    return LM.all() ? 0 : insert(LM);
  }

  /// Look up the compact index for existing lane mask \p LM (0 for all-lanes).
  /// \param LM Non-empty lane mask previously inserted.
  /// \return Compact index for \p LM (0 when \p LM covers all lanes).
  uint32_t getIndexForLaneMask(LaneBitmask LM) const {
    assert(LM.any());
    return LM.all() ? 0 : find(LM);
  }
};

/// POD base for every RDF graph node, storing attributes and payload in a union.
///
/// Derived node types only add member functions; all data lives here so that
/// nodes stay within \c NodeAllocator::NodeMemSize bytes.
struct NodeBase {
public:
  /// Construct a zero-initialized node base (POD).
  NodeBase() = default;

  /// Return the node type bits from \c Attrs.
  /// \return Node type field from the packed attributes.
  uint16_t getType() const { return NodeAttrs::type(Attrs); }
  /// Return the node kind bits from \c Attrs.
  /// \return Node kind field from the packed attributes.
  uint16_t getKind() const { return NodeAttrs::kind(Attrs); }
  /// Return the flag bits from \c Attrs.
  /// \return Flag field from the packed attributes.
  uint16_t getFlags() const { return NodeAttrs::flags(Attrs); }
  /// Return the id of the next node in the circular member chain.
  /// \return Id of the next node, or 0 when none.
  NodeId getNext() const { return Next; }

  /// Return the raw packed attribute word.
  /// \return Packed type, kind, and flag attributes.
  uint16_t getAttrs() const { return Attrs; }
  /// Replace the packed attribute word with \p A.
  /// \param A New packed attributes.
  void setAttrs(uint16_t A) { Attrs = A; }
  /// Replace only the flag bits with \p F.
  /// \param F New flag bits.
  void setFlags(uint16_t F) { setAttrs(NodeAttrs::set_flags(getAttrs(), F)); }

  /// Insert node \p NA after this node in the circular chain.
  /// \param NA Node to append after this one.
  LLVM_ABI void append(Node NA);

  /// Initialize all members to 0.
  void init() { memset(this, 0, sizeof *this); }

  /// Set the next-node id in the circular chain to \p N.
  /// \param N Id of the next node.
  void setNext(NodeId N) { Next = N; }

protected:
  /// Packed type, kind, and flag attributes.
  uint16_t Attrs;
  /// Reserved padding / unused bits.
  uint16_t Reserved;
  /// Id of the next node in the circular chain.
  NodeId Next;
  // Definitions of nested types. Using anonymous nested structs would make
  // this class definition clearer, but unnamed structs are not a part of
  // the standard.
  /// Payload for definition nodes: first reached def and use ids.
  struct Def_struct {
    /// Id of the first reached definition.
    NodeId DD;
    /// Id of the first reached use.
    NodeId DU;
  };
  /// Payload for phi-use nodes: predecessor block id.
  struct PhiU_struct {
    /// Id of the predecessor block for a phi use.
    NodeId PredB;
  };
  /// Payload for code (container) nodes.
  struct Code_struct {
    /// Pointer to the actual code object (MI, MBB, or MF).
    void *CP;
    /// Id of the first member in the circular list.
    NodeId FirstM;
    /// Id of the last member in the circular list.
    NodeId LastM;
  };
  /// Payload for reference (def/use) nodes.
  struct Ref_struct {
    /// Id of the reaching definition.
    NodeId RD;
    /// Id of the next sibling sharing the same reaching def.
    NodeId Sib;
    union {
      /// Reached-def / reached-use links for definition nodes.
      Def_struct Def;
      /// Predecessor block for phi-use nodes.
      PhiU_struct PhiU;
    };
    union {
      /// Non-phi refs point to a machine operand.
      MachineOperand *Op;
      /// Phi refs store register info directly.
      PackedRegisterRef PR;
    };
  };

  /// Type-specific payload for this node.
  union {
    /// Reference-node payload.
    Ref_struct RefData;
    /// Code-node payload.
    Code_struct CodeData;
  };
};
// The allocator allocates chunks of 32 bytes for each node. The fact that
// each node takes 32 bytes in memory is used for fast translation between
// the node id and the node address.
static_assert(sizeof(NodeBase) <= NodeAllocator::NodeMemSize,
              "NodeBase must be at most NodeAllocator::NodeMemSize bytes");

/// Small vector of node addresses.
using NodeList = SmallVector<Node, 4>;
/// Ordered set of node ids.
using NodeSet = std::set<NodeId>;

/// Reference node representing a register def or use.
struct RefNode : public NodeBase {
  /// Construct a default reference node.
  RefNode() = default;

  /// Return the register reference for this node in graph \p G.
  /// \param G Owning data-flow graph used to unpack register info.
  /// \return Register and lane mask for this reference.
  LLVM_ABI RegisterRef getRegRef(const DataFlowGraph &G) const;

  /// Return the machine operand associated with this non-phi reference.
  /// \return Reference to the backing machine operand.
  MachineOperand &getOp() {
    assert(!(getFlags() & NodeAttrs::PhiRef));
    return *RefData.Op;
  }

  /// Set this node's register from packed reference \p RR in graph \p G.
  /// \param RR Register and lane mask to store.
  /// \param G Owning data-flow graph used to pack the reference.
  LLVM_ABI void setRegRef(RegisterRef RR, DataFlowGraph &G);
  /// Set this node's register from machine operand \p Op in graph \p G.
  /// \param Op Machine operand describing the register.
  /// \param G Owning data-flow graph.
  LLVM_ABI void setRegRef(MachineOperand *Op, DataFlowGraph &G);

  /// Return the id of the reaching definition.
  /// \return Id of the reaching def node, or 0 when none.
  NodeId getReachingDef() const { return RefData.RD; }
  /// Set the reaching definition id to \p RD.
  /// \param RD Id of the reaching def node.
  void setReachingDef(NodeId RD) { RefData.RD = RD; }

  /// Return the id of the next sibling sharing the same reaching def.
  /// \return Id of the sibling reference, or 0 when none.
  NodeId getSibling() const { return RefData.Sib; }
  /// Set the sibling id to \p Sib.
  /// \param Sib Id of the sibling reference node.
  void setSibling(NodeId Sib) { RefData.Sib = Sib; }

  /// Return true if this reference is a use.
  /// \return True when this node's kind is \c NodeAttrs::Use.
  bool isUse() const {
    assert(getType() == NodeAttrs::Ref);
    return getKind() == NodeAttrs::Use;
  }

  /// Return true if this reference is a definition.
  /// \return True when this node's kind is \c NodeAttrs::Def.
  bool isDef() const {
    assert(getType() == NodeAttrs::Ref);
    return getKind() == NodeAttrs::Def;
  }

  /// Return the next circular-list ref matching \p RR and predicate \p P.
  ///
  /// Walks the circular member list starting after this node. If \p NextOnly
  /// is true, only the immediate next candidate is considered.
  /// \param RR Register reference to match.
  /// \param P Predicate invoked on candidate nodes.
  /// \param NextOnly If true, stop after examining the next related slot.
  /// \param G Owning data-flow graph.
  /// \return Matching reference address, or a null ref when none is found.
  template <typename Predicate>
  Ref getNextRef(RegisterRef RR, Predicate P, bool NextOnly,
                 const DataFlowGraph &G);
  /// Return the owning instruction (code) node in graph \p G.
  /// \param G Owning data-flow graph.
  /// \return Address of the instruction that owns this reference.
  LLVM_ABI Node getOwner(const DataFlowGraph &G);
};

/// Definition reference node with reached-def and reached-use links.
struct DefNode : public RefNode {
  /// Return the id of the first reached definition.
  /// \return Id of the first reached def, or 0 when none.
  NodeId getReachedDef() const { return RefData.Def.DD; }
  /// Set the first reached definition id to \p D.
  /// \param D Id of the reached def.
  void setReachedDef(NodeId D) { RefData.Def.DD = D; }
  /// Return the id of the first reached use.
  /// \return Id of the first reached use, or 0 when none.
  NodeId getReachedUse() const { return RefData.Def.DU; }
  /// Set the first reached use id to \p U.
  /// \param U Id of the reached use.
  void setReachedUse(NodeId U) { RefData.Def.DU = U; }

  /// Link this def (id \p Self) into the reaching-def chain of \p DA.
  /// \param Self Id of this definition node.
  /// \param DA Reaching definition to link from.
  LLVM_ABI void linkToDef(NodeId Self, Def DA);
};

/// Use reference node that can link to a reaching definition.
struct UseNode : public RefNode {
  /// Link this use (id \p Self) to reaching definition \p DA.
  /// \param Self Id of this use node.
  /// \param DA Reaching definition to link from.
  LLVM_ABI void linkToDef(NodeId Self, Def DA);
};

/// Phi-use reference that records its predecessor basic block.
struct PhiUseNode : public UseNode {
  /// Return the id of the predecessor block for this phi use.
  /// \return Id of the predecessor block node.
  NodeId getPredecessor() const {
    assert(getFlags() & NodeAttrs::PhiRef);
    return RefData.PhiU.PredB;
  }
  /// Set the predecessor block id to \p B.
  /// \param B Id of the predecessor block node.
  void setPredecessor(NodeId B) {
    assert(getFlags() & NodeAttrs::PhiRef);
    RefData.PhiU.PredB = B;
  }
};

/// Code container node holding a circular list of member nodes.
struct CodeNode : public NodeBase {
  /// Return the underlying code pointer cast to type \p T.
  /// \return Underlying code pointer cast to \p T.
  template <typename T> T getCode() const { //
    return static_cast<T>(CodeData.CP);
  }
  /// Set the underlying code pointer to \p C.
  /// \param C Pointer to the associated IR/Machine object.
  void setCode(void *C) { CodeData.CP = C; }

  /// Return the first member in the circular list, or null.
  /// \param G Owning data-flow graph.
  /// \return Address of the first member, or a null node when empty.
  LLVM_ABI Node getFirstMember(const DataFlowGraph &G) const;
  /// Return the last member in the circular list, or null.
  /// \param G Owning data-flow graph.
  /// \return Address of the last member, or a null node when empty.
  LLVM_ABI Node getLastMember(const DataFlowGraph &G) const;
  /// Append member \p NA to this container in graph \p G.
  /// \param NA Member node to add.
  /// \param G Owning data-flow graph.
  LLVM_ABI void addMember(Node NA, const DataFlowGraph &G);
  /// Insert member \p NA after existing member \p MA in graph \p G.
  /// \param MA Existing member after which to insert.
  /// \param NA Member node to insert.
  /// \param G Owning data-flow graph.
  LLVM_ABI void addMemberAfter(Node MA, Node NA, const DataFlowGraph &G);
  /// Remove member \p NA from this container in graph \p G.
  /// \param NA Member node to remove.
  /// \param G Owning data-flow graph.
  LLVM_ABI void removeMember(Node NA, const DataFlowGraph &G);

  /// Return all members of this container in graph \p G.
  /// \param G Owning data-flow graph.
  /// \return List of all member node addresses.
  LLVM_ABI NodeList members(const DataFlowGraph &G) const;
  /// Return members of this container for which predicate \p P is true.
  /// \param P Predicate invoked on each member.
  /// \param G Owning data-flow graph.
  /// \return List of member addresses for which \p P is true.
  template <typename Predicate>
  NodeList members_if(Predicate P, const DataFlowGraph &G) const;
};

/// Instruction code node (phi or statement) owned by a block.
struct InstrNode : public CodeNode {
  /// Return the owning block node in graph \p G.
  /// \param G Owning data-flow graph.
  /// \return Address of the owning block node.
  LLVM_ABI Node getOwner(const DataFlowGraph &G);
};

/// Phi instruction node; has no backing \c MachineInstr.
struct PhiNode : public InstrNode {
  /// Return null; phi nodes are not tied to a machine instruction.
  /// \return Always \c nullptr.
  MachineInstr *getCode() const { return nullptr; }
};

/// Statement node corresponding to a machine instruction.
struct StmtNode : public InstrNode {
  /// Return the machine instruction for this statement.
  /// \return Pointer to the backing \c MachineInstr.
  MachineInstr *getCode() const { //
    return CodeNode::getCode<MachineInstr *>();
  }
};

/// Basic-block code node containing instruction members.
struct BlockNode : public CodeNode {
  /// Return the machine basic block for this node.
  /// \return Pointer to the backing \c MachineBasicBlock.
  MachineBasicBlock *getCode() const {
    return CodeNode::getCode<MachineBasicBlock *>();
  }

  /// Insert phi node \p PA into this block in graph \p G.
  /// \param PA Phi node to add.
  /// \param G Owning data-flow graph.
  LLVM_ABI void addPhi(Phi PA, const DataFlowGraph &G);
};

/// Function code node containing block members.
struct FuncNode : public CodeNode {
  /// Return the machine function for this node.
  /// \return Pointer to the backing \c MachineFunction.
  MachineFunction *getCode() const {
    return CodeNode::getCode<MachineFunction *>();
  }

  /// Return the block node for machine basic block \p BB in graph \p G.
  /// \param BB Machine basic block to look up.
  /// \param G Owning data-flow graph.
  /// \return Block node address corresponding to \p BB.
  LLVM_ABI Block findBlock(const MachineBasicBlock *BB,
                           const DataFlowGraph &G) const;
  /// Return the entry block of this function in graph \p G.
  /// \param G Owning data-flow graph.
  /// \return Block node address for the function entry block.
  LLVM_ABI Block getEntryBlock(const DataFlowGraph &G);
};

/// SSA-style register data-flow graph for a machine function.
struct DataFlowGraph {
  /// Construct a graph for \p mf using default target operand info.
  /// \param mf Machine function to analyze.
  /// \param tii Target instruction info.
  /// \param tri Target register info.
  /// \param mdt Machine dominator tree.
  /// \param mdf Machine dominance frontier.
  LLVM_ABI DataFlowGraph(MachineFunction &mf, const TargetInstrInfo &tii,
                         const TargetRegisterInfo &tri,
                         const MachineDominatorTree &mdt,
                         const MachineDominanceFrontier &mdf);
  /// Construct a graph for \p mf using custom target operand info \p toi.
  /// \param mf Machine function to analyze.
  /// \param tii Target instruction info.
  /// \param tri Target register info.
  /// \param mdt Machine dominator tree.
  /// \param mdf Machine dominance frontier.
  /// \param toi Target operand classification hooks.
  LLVM_ABI DataFlowGraph(MachineFunction &mf, const TargetInstrInfo &tii,
                         const TargetRegisterInfo &tri,
                         const MachineDominatorTree &mdt,
                         const MachineDominanceFrontier &mdf,
                         const TargetOperandInfo &toi);

  /// Configuration for which registers to track while building the graph.
  struct Config {
    /// Construct a default configuration with no extra options.
    Config() = default;
    /// Construct with build option flags \p Opts.
    /// \param Opts Bitmask of \c BuildOptions.
    Config(unsigned Opts) : Options(Opts) {}
    /// Construct tracking only registers from classes \p RCs.
    /// \param RCs Target register classes to track.
    Config(ArrayRef<const TargetRegisterClass *> RCs) : Classes(RCs) {}
    /// Construct tracking only physical registers listed in \p Track.
    /// \param Track Physical registers to track.
    Config(ArrayRef<MCPhysReg> Track) : TrackRegs(Track.begin(), Track.end()) {}
    /// Construct tracking only register ids listed in \p Track.
    /// \param Track Register ids to track.
    Config(ArrayRef<RegisterId> Track)
        : TrackRegs(Track.begin(), Track.end()) {}

    /// Bitmask of \c BuildOptions flags.
    unsigned Options = BuildOptions::None;
    /// Optional register classes limiting which registers are tracked.
    SmallVector<const TargetRegisterClass *> Classes;
    /// Explicit set of register ids to track, if non-empty.
    std::set<RegisterId> TrackRegs;
  };

  /// Return the node pointer for id \p N.
  /// \param N Node id to resolve.
  /// \return Pointer to the node stored under \p N.
  LLVM_ABI NodeBase *ptr(NodeId N) const;
  /// Return the node pointer for id \p N cast to type \p T.
  /// \param N Node id to resolve.
  /// \return Pointer to the node stored under \p N, cast to \p T.
  template <typename T> T ptr(NodeId N) const { //
    return static_cast<T>(ptr(N));
  }

  /// Return the node id for pointer \p P.
  /// \param P Node address allocated in this graph.
  /// \return Node id corresponding to \p P.
  LLVM_ABI NodeId id(const NodeBase *P) const;

  /// Return a typed \c NodeAddr for id \p N.
  /// \param N Node id to wrap.
  /// \return Node address pairing the pointer for \p N with \p N.
  template <typename T> NodeAddr<T> addr(NodeId N) const {
    return {ptr<T>(N), N};
  }

  /// Return the function node for this graph.
  /// \return Address of the function code node.
  Func getFunc() const { return TheFunc; }
  /// Return the underlying machine function.
  /// \return Reference to the analyzed machine function.
  MachineFunction &getMF() const { return MF; }
  /// Return the target instruction info.
  /// \return Target instruction info used by this graph.
  const TargetInstrInfo &getTII() const { return TII; }
  /// Return the target register info.
  /// \return Target register info used by this graph.
  const TargetRegisterInfo &getTRI() const { return TRI; }
  /// Return the physical-register info helper.
  /// \return Physical-register info for the analyzed function.
  const PhysicalRegisterInfo &getPRI() const { return PRI; }
  /// Return the machine dominator tree.
  /// \return Dominator tree used while building the graph.
  const MachineDominatorTree &getDT() const { return MDT; }
  /// Return the machine dominance frontier.
  /// \return Dominance frontier used while building the graph.
  const MachineDominanceFrontier &getDF() const { return MDF; }
  /// Return the aggregated live-in registers for the function.
  /// \return Aggregate of registers live into the function.
  const RegisterAggr &getLiveIns() const { return LiveIns; }

  /// Stack of reaching definitions for one register during graph build.
  struct DefStack {
    /// Construct an empty definition stack.
    DefStack() = default;

    /// Return true if the stack has no active definitions.
    /// \return True when there are no active definitions on the stack.
    bool empty() const { return Stack.empty() || top() == bottom(); }

  private:
    using value_type = Def;
    struct Iterator {
      using value_type = DefStack::value_type;

      Iterator &up() {
        Pos = DS.nextUp(Pos);
        return *this;
      }
      Iterator &down() {
        Pos = DS.nextDown(Pos);
        return *this;
      }

      value_type operator*() const {
        assert(Pos >= 1);
        return DS.Stack[Pos - 1];
      }
      const value_type *operator->() const {
        assert(Pos >= 1);
        return &DS.Stack[Pos - 1];
      }
      bool operator==(const Iterator &It) const { return Pos == It.Pos; }
      bool operator!=(const Iterator &It) const { return Pos != It.Pos; }

    private:
      friend struct DefStack;

      LLVM_ABI Iterator(const DefStack &S, bool Top);

      // Pos-1 is the index in the StorageType object that corresponds to
      // the top of the DefStack.
      const DefStack &DS;
      unsigned Pos;
    };

  public:
    /// Iterator over active definitions on this stack.
    using iterator = Iterator;

    /// Return an iterator to the top (most recent) definition.
    /// \return Iterator positioned at the most recent definition.
    iterator top() const { return Iterator(*this, true); }
    /// Return an iterator to the bottom sentinel of the stack.
    /// \return Iterator positioned at the bottom sentinel.
    iterator bottom() const { return Iterator(*this, false); }
    /// Return the number of active definitions on the stack.
    /// \return Count of active definitions excluding delimiters.
    LLVM_ABI unsigned size() const;

    /// Push definition \p DA onto the stack.
    /// \param DA Definition to push.
    void push(Def DA) { Stack.push_back(DA); }
    /// Pop the top definition from the stack.
    LLVM_ABI void pop();
    /// Mark the start of block \p N on the stack (push a delimiter).
    /// \param N Block node id whose region begins.
    LLVM_ABI void start_block(NodeId N);
    /// Clear all definitions recorded for block \p N.
    /// \param N Block node id whose region is cleared.
    LLVM_ABI void clear_block(NodeId N);

  private:
    friend struct Iterator;

    using StorageType = std::vector<value_type>;

    bool isDelimiter(const StorageType::value_type &P, NodeId N = 0) const {
      return (P.Addr == nullptr) && (N == 0 || P.Id == N);
    }

    LLVM_ABI unsigned nextUp(unsigned P) const;
    LLVM_ABI unsigned nextDown(unsigned P) const;

    StorageType Stack;
  };

  /// Map from register id to its stack of reaching definitions.
  using DefStackMap = DenseMap<RegisterId, DefStack>;

  /// Build the data-flow graph using configuration \p config.
  /// \param config Build options and register tracking filters.
  LLVM_ABI void build(const Config &config);
  /// Build the data-flow graph with default configuration.
  void build() { build(Config()); }

  /// Push all definitions from instruction \p IA onto stacks in \p DM.
  /// \param IA Instruction whose defs are pushed.
  /// \param DM Per-register definition stacks to update.
  LLVM_ABI void pushAllDefs(Instr IA, DefStackMap &DM);
  /// Record that block \p B becomes active in definition map \p DefM.
  /// \param B Block node id.
  /// \param DefM Per-register definition stacks.
  LLVM_ABI void markBlock(NodeId B, DefStackMap &DefM);
  /// Release definitions recorded for block \p B in map \p DefM.
  /// \param B Block node id.
  /// \param DefM Per-register definition stacks.
  LLVM_ABI void releaseBlock(NodeId B, DefStackMap &DefM);

  /// Pack register reference \p RR into a compact storage form.
  /// \param RR Register reference to pack.
  /// \return Compact packed form of \p RR.
  PackedRegisterRef pack(RegisterRef RR) {
    return {RR.Id, LMI.getIndexForLaneMask(RR.Mask)};
  }
  /// Pack register reference \p RR into a compact storage form (const).
  /// \param RR Register reference to pack.
  /// \return Compact packed form of \p RR.
  PackedRegisterRef pack(RegisterRef RR) const {
    return {RR.Id, LMI.getIndexForLaneMask(RR.Mask)};
  }
  /// Unpack compact register reference \p PR into a \c RegisterRef.
  /// \param PR Packed register reference.
  /// \return Unpacked register reference for \p PR.
  RegisterRef unpack(PackedRegisterRef PR) const {
    return RegisterRef(PR.Id, LMI.getLaneMaskForIndex(PR.MaskId));
  }

  /// Build a register reference from register \p Reg and subregister \p Sub.
  /// \param Reg Register number.
  /// \param Sub Subregister index.
  /// \return Register reference for \p Reg and \p Sub.
  LLVM_ABI RegisterRef makeRegRef(unsigned Reg, unsigned Sub) const;
  /// Build a register reference from machine operand \p Op.
  /// \param Op Machine operand describing the register.
  /// \return Register reference described by \p Op.
  LLVM_ABI RegisterRef makeRegRef(const MachineOperand &Op) const;

  /// Return the next related reference after \p RA in instruction \p IA.
  /// \param IA Instruction owning the references.
  /// \param RA Starting reference.
  /// \return Next related reference, or a null ref when none remains.
  LLVM_ABI Ref getNextRelated(Instr IA, Ref RA) const;
  /// Return or create the next shadow reference after \p RA in \p IA.
  /// \param IA Instruction owning the references.
  /// \param RA Starting reference.
  /// \param Create If true, create a shadow ref when missing.
  /// \return Next shadow reference address.
  LLVM_ABI Ref getNextShadow(Instr IA, Ref RA, bool Create);

  /// Return all references related to \p RA within instruction \p IA.
  /// \param IA Instruction owning the references.
  /// \param RA Reference whose related set is requested.
  /// \return List of references related to \p RA.
  LLVM_ABI NodeList getRelatedRefs(Instr IA, Ref RA) const;

  /// Return the block node for machine basic block \p BB.
  /// \param BB Machine basic block previously recorded in this graph.
  /// \return Block node address for \p BB.
  Block findBlock(MachineBasicBlock *BB) const { return BlockNodes.at(BB); }

  /// Unlink use \p UA from data-flow links, optionally removing from owner.
  /// \param UA Use to unlink.
  /// \param RemoveFromOwner If true, also remove \p UA from its instruction.
  void unlinkUse(Use UA, bool RemoveFromOwner) {
    unlinkUseDF(UA);
    if (RemoveFromOwner)
      removeFromOwner(UA);
  }

  /// Unlink def \p DA from data-flow links, optionally removing from owner.
  /// \param DA Definition to unlink.
  /// \param RemoveFromOwner If true, also remove \p DA from its instruction.
  void unlinkDef(Def DA, bool RemoveFromOwner) {
    unlinkDefDF(DA);
    if (RemoveFromOwner)
      removeFromOwner(DA);
  }

  /// Return true if register reference \p RR is tracked by this graph.
  /// \param RR Register reference to test.
  /// \return True when \p RR is among the tracked registers.
  LLVM_ABI bool isTracked(RegisterRef RR) const;
  /// Return true if statement \p S has a reference that is not tracked.
  /// \param S Statement to inspect.
  /// \param IgnoreReserved If true, reserved registers do not count.
  /// \return True when \p S contains an untracked reference.
  LLVM_ABI bool hasUntrackedRef(Stmt S, bool IgnoreReserved = true) const;

  // Some useful filters.
  /// Return true if \p BA is a reference node of kind \p Kind.
  /// \param BA Node to test.
  /// \return True when \p BA is a ref node whose kind equals \p Kind.
  template <uint16_t Kind> static bool IsRef(const Node BA) {
    return BA.Addr->getType() == NodeAttrs::Ref && BA.Addr->getKind() == Kind;
  }

  /// Return true if \p BA is a code node of kind \p Kind.
  /// \param BA Node to test.
  /// \return True when \p BA is a code node whose kind equals \p Kind.
  template <uint16_t Kind> static bool IsCode(const Node BA) {
    return BA.Addr->getType() == NodeAttrs::Code && BA.Addr->getKind() == Kind;
  }

  /// Return true if \p BA is a definition reference.
  /// \param BA Node to test.
  /// \return True when \p BA is a definition reference node.
  static bool IsDef(const Node BA) {
    return BA.Addr->getType() == NodeAttrs::Ref &&
           BA.Addr->getKind() == NodeAttrs::Def;
  }

  /// Return true if \p BA is a use reference.
  /// \param BA Node to test.
  /// \return True when \p BA is a use reference node.
  static bool IsUse(const Node BA) {
    return BA.Addr->getType() == NodeAttrs::Ref &&
           BA.Addr->getKind() == NodeAttrs::Use;
  }

  /// Return true if \p BA is a phi instruction node.
  /// \param BA Node to test.
  /// \return True when \p BA is a phi code node.
  static bool IsPhi(const Node BA) {
    return BA.Addr->getType() == NodeAttrs::Code &&
           BA.Addr->getKind() == NodeAttrs::Phi;
  }

  /// Return true if \p DA is a preserving, non-undef definition.
  /// \param DA Definition to test.
  /// \return True when \p DA is preserving and not marked undef.
  static bool IsPreservingDef(const Def DA) {
    uint16_t Flags = DA.Addr->getFlags();
    return (Flags & NodeAttrs::Preserving) && !(Flags & NodeAttrs::Undef);
  }

private:
  void reset();

  RegisterAggr getLandingPadLiveIns() const;

  Node newNode(uint16_t Attrs);
  Node cloneNode(const Node B);
  Use newUse(Instr Owner, MachineOperand &Op, uint16_t Flags = NodeAttrs::None);
  PhiUse newPhiUse(Phi Owner, RegisterRef RR, Block PredB,
                   uint16_t Flags = NodeAttrs::PhiRef);
  Def newDef(Instr Owner, MachineOperand &Op, uint16_t Flags = NodeAttrs::None);
  Def newDef(Instr Owner, RegisterRef RR, uint16_t Flags = NodeAttrs::PhiRef);
  Phi newPhi(Block Owner);
  Stmt newStmt(Block Owner, MachineInstr *MI);
  Block newBlock(Func Owner, MachineBasicBlock *BB);
  Func newFunc(MachineFunction *MF);

  template <typename Predicate>
  std::pair<Ref, Ref> locateNextRef(Instr IA, Ref RA, Predicate P) const;

  using BlockRefsMap = RegisterAggrMap<NodeId>;

  void buildStmt(Block BA, MachineInstr &In);
  void recordDefsForDF(BlockRefsMap &PhiM, BlockRefsMap &PhiClobberM, Block BA);
  void buildPhis(BlockRefsMap &PhiM, Block BA,
                 const DefStackMap &DefM = DefStackMap());
  void removeUnusedPhis();

  void pushClobbers(Instr IA, DefStackMap &DM);
  void pushDefs(Instr IA, DefStackMap &DM);
  template <typename T> void linkRefUp(Instr IA, NodeAddr<T> TA, DefStack &DS);
  template <typename Predicate>
  void linkStmtRefs(DefStackMap &DefM, Stmt SA, Predicate P);
  void linkBlockRefs(DefStackMap &DefM, BlockRefsMap &PhiClobberM, Block BA);

  LLVM_ABI void unlinkUseDF(Use UA);
  LLVM_ABI void unlinkDefDF(Def DA);

  void removeFromOwner(Ref RA) {
    Instr IA = RA.Addr->getOwner(*this);
    IA.Addr->removeMember(RA, *this);
  }

  // Default TOI object, if not given in the constructor.
  std::unique_ptr<TargetOperandInfo> DefaultTOI;

  MachineFunction &MF;
  const TargetInstrInfo &TII;
  const TargetRegisterInfo &TRI;
  const PhysicalRegisterInfo PRI;
  const MachineDominatorTree &MDT;
  const MachineDominanceFrontier &MDF;
  const TargetOperandInfo &TOI;

  RegisterAggr LiveIns;
  Func TheFunc;
  NodeAllocator Memory;
  // Local map:  MachineBasicBlock -> NodeAddr<BlockNode*>
  std::map<MachineBasicBlock *, Block> BlockNodes;
  // Lane mask map.
  LaneMaskIndex LMI;

  Config BuildCfg;
  std::set<unsigned> TrackedUnits;
  BitVector ReservedRegs;
}; // struct DataFlowGraph

/// Return the next circular-list ref matching \p RR and predicate \p P.
/// \param RR Register reference to match.
/// \param P Predicate invoked on candidate nodes.
/// \param NextOnly If true, stop after examining the next related slot.
/// \param G Owning data-flow graph.
/// \return Matching reference address, or a null ref when none is found.
template <typename Predicate>
Ref RefNode::getNextRef(RegisterRef RR, Predicate P, bool NextOnly,
                        const DataFlowGraph &G) {
  // Get the "Next" reference in the circular list that references RR and
  // satisfies predicate "Pred".
  auto NA = G.addr<NodeBase *>(getNext());

  while (NA.Addr != this) {
    if (NA.Addr->getType() == NodeAttrs::Ref) {
      Ref RA = NA;
      if (G.getPRI().equal_to(RA.Addr->getRegRef(G), RR) && P(NA))
        return NA;
      if (NextOnly)
        break;
      NA = G.addr<NodeBase *>(NA.Addr->getNext());
    } else {
      // We've hit the beginning of the chain.
      assert(NA.Addr->getType() == NodeAttrs::Code);
      // Make sure we stop here with NextOnly. Otherwise we can return the
      // wrong ref. Consider the following while creating/linking shadow uses:
      //   -> code -> sr1 -> sr2 -> [back to code]
      // Say that shadow refs sr1, and sr2 have been linked, but we need to
      // create and link another one. Starting from sr2, we'd hit the code
      // node and return sr1 if the iteration didn't stop here.
      if (NextOnly)
        break;
      Code CA = NA;
      NA = CA.Addr->getFirstMember(G);
    }
  }
  // Return the equivalent of "nullptr" if such a node was not found.
  return Ref();
}

/// Return members of this container for which predicate \p P is true.
/// \param P Predicate invoked on each member.
/// \param G Owning data-flow graph.
/// \return List of member addresses for which \p P is true.
template <typename Predicate>
NodeList CodeNode::members_if(Predicate P, const DataFlowGraph &G) const {
  NodeList MM;
  auto M = getFirstMember(G);
  if (M.Id == 0)
    return MM;

  while (M.Addr != this) {
    if (P(M))
      MM.push_back(M);
    M = G.addr<NodeBase *>(M.Addr->getNext());
  }
  return MM;
}

/// Helper wrapping an object and its graph for stream printing.
template <typename T> struct Print {
  /// Construct a print wrapper for \p x in graph \p g.
  /// \param x Object to print.
  /// \param g Data-flow graph providing context.
  Print(const T &x, const DataFlowGraph &g) : Obj(x), G(g) {}

  /// Object being printed.
  const T &Obj;
  /// Data-flow graph providing printing context.
  const DataFlowGraph &G;
};

/// Deduce \c Print\<T\> from an object and its data-flow graph.
template <typename T> Print(const T &, const DataFlowGraph &) -> Print<T>;

/// Print wrapper specialized for typed node addresses.
template <typename T> struct PrintNode : Print<NodeAddr<T>> {
  /// Construct a print wrapper for node address \p x in graph \p g.
  /// \param x Node address to print.
  /// \param g Data-flow graph providing context.
  PrintNode(const NodeAddr<T> &x, const DataFlowGraph &g)
      : Print<NodeAddr<T>>(x, g) {}
};

/// Write a printed register reference to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the register reference.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<RegisterRef> &P);
/// Write a printed node id to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the node id.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<NodeId> &P);
/// Write a printed definition node to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the definition.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<Def> &P);
/// Write a printed use node to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the use.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<Use> &P);
/// Write a printed phi-use node to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the phi use.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<PhiUse> &P);
/// Write a printed reference node to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the reference.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<Ref> &P);
/// Write a printed node list to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the node list.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<NodeList> &P);
/// Write a printed node set to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the node set.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<NodeSet> &P);
/// Write a printed phi node to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the phi.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<Phi> &P);
/// Write a printed statement node to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the statement.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<Stmt> &P);
/// Write a printed instruction node to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the instruction.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<Instr> &P);
/// Write a printed block node to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the block.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<Block> &P);
/// Write a printed function node to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the function.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<Func> &P);
/// Write a printed register set to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the register set.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<RegisterSet> &P);
/// Write a printed register aggregate to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the register aggregate.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Print<RegisterAggr> &P);
/// Write a printed definition stack to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the definition stack.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const Print<DataFlowGraph::DefStack> &P);

} // end namespace rdf
} // end namespace llvm

#endif // LLVM_CODEGEN_RDFGRAPH_H
