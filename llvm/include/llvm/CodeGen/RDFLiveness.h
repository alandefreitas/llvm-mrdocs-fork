//===- RDFLiveness.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Recalculate the liveness information given a data flow graph.
// This includes block live-ins and kill flags.

#ifndef LLVM_CODEGEN_RDFLIVENESS_H
#define LLVM_CODEGEN_RDFLIVENESS_H

#include "RDFGraph.h"
#include "RDFRegisters.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/MC/LaneBitmask.h"
#include <map>
#include <set>
#include <unordered_set>
#include <utility>

namespace llvm {

class MachineBasicBlock;
class MachineDominanceFrontier;
class MachineDominatorTree;
class MachineRegisterInfo;
class TargetRegisterInfo;

namespace rdf {
namespace detail {

using NodeRef = std::pair<NodeId, LaneBitmask>;

} // namespace detail
} // namespace rdf
} // namespace llvm

namespace std {

template <> struct hash<llvm::rdf::detail::NodeRef> {
  std::size_t operator()(llvm::rdf::detail::NodeRef R) const {
    return std::hash<llvm::rdf::NodeId>{}(R.first) ^
           std::hash<llvm::LaneBitmask::Type>{}(R.second.getAsInteger());
  }
};

} // namespace std

namespace llvm::rdf {

/// Recalculates register liveness from an RDF data-flow graph.
///
/// Computes block live-ins and kill flags from reaching definitions and
/// reached uses in the graph.
struct Liveness {
public:
  /// Map from basic block to the aggregate of registers live in that block.
  using LiveMapType = RegisterAggrMap<MachineBasicBlock *>;
  /// Pair of a reference node id and the lane mask of the referenced register.
  using NodeRef = detail::NodeRef;
  /// Unordered set of node-reference pairs.
  using NodeRefSet = std::unordered_set<NodeRef>;
  /// Map from register id to the set of node references using that register.
  using RefMap = DenseMap<RegisterId, NodeRefSet>;

  /// Construct a liveness analyzer for machine register info \p mri and
  /// data-flow graph \p g.
  /// \param mri Machine register info for the function under analysis.
  /// \param g Data-flow graph whose dominance and register info are used.
  Liveness(MachineRegisterInfo &mri, const DataFlowGraph &g)
      : DFG(g), TRI(g.getTRI()), PRI(g.getPRI()), MDT(g.getDT()),
        MDF(g.getDF()), LiveMap(g.getPRI()), Empty(), NoRegs(g.getPRI()) {}

  /// Return all reaching definitions of register \p RefRR at reference \p RefA.
  ///
  /// The returned sequence is ordered from the closest reaching def upward.
  /// It ends at a reaching phi def, or when \p RefRR is covered by the defs
  /// collected so far (unless \p FullChain is set). Already-defined register
  /// references may be passed in \p DefRRs to continue a partial traversal.
  /// \param RefRR Register reference whose reaching defs are sought.
  /// \param RefA Reference node at which the upward search starts.
  /// \param TopShadows If true, also enqueue reaching defs of related shadow
  ///        references of \p RefA.
  /// \param FullChain If true, keep collecting past covering defs (and treat
  ///        phi defs as non-covering) to build a full chain.
  /// \param DefRRs Register aggregate of defs already encountered in a prior
  ///        continuation of the search.
  /// \return Ordered list of reaching definition nodes.
  LLVM_ABI NodeList getAllReachingDefs(RegisterRef RefRR,
                                       NodeAddr<RefNode *> RefA,
                                       bool TopShadows, bool FullChain,
                                       const RegisterAggr &DefRRs);

  /// Return all reaching definitions of the register referenced by \p RefA.
  ///
  /// Equivalent to calling the full overload with \p TopShadows and
  /// \p FullChain false and an empty intervening-def set.
  /// \param RefA Reference node whose register's reaching defs are sought.
  /// \return Ordered list of reaching definition nodes.
  NodeList getAllReachingDefs(NodeAddr<RefNode *> RefA) {
    return getAllReachingDefs(RefA.Addr->getRegRef(DFG), RefA, false, false,
                              NoRegs);
  }

  /// Return all reaching definitions of register \p RefRR at reference \p RefA.
  ///
  /// Equivalent to calling the full overload with \p TopShadows and
  /// \p FullChain false and an empty intervening-def set.
  /// \param RefRR Register reference whose reaching defs are sought.
  /// \param RefA Reference node at which the upward search starts.
  /// \return Ordered list of reaching definition nodes.
  NodeList getAllReachingDefs(RegisterRef RefRR, NodeAddr<RefNode *> RefA) {
    return getAllReachingDefs(RefRR, RefA, false, false, NoRegs);
  }

  /// Return all uses reached by definition \p DefA for register \p RefRR.
  ///
  /// Uses covered by intervening definitions in \p DefRRs are excluded.
  /// \param RefRR Register reference of interest.
  /// \param DefA Definition node whose reached uses are collected.
  /// \param DefRRs Aggregate of intervening definitions that already cover
  ///        parts of \p RefRR.
  /// \return Set of use nodes reached by \p DefA for \p RefRR.
  LLVM_ABI NodeSet getAllReachedUses(RegisterRef RefRR,
                                     NodeAddr<DefNode *> DefA,
                                     const RegisterAggr &DefRRs);

  /// Return all uses reached by definition \p DefA for register \p RefRR.
  ///
  /// Equivalent to calling the overload with an empty intervening-def set.
  /// \param RefRR Register reference of interest.
  /// \param DefA Definition node whose reached uses are collected.
  /// \return Set of use nodes reached by \p DefA for \p RefRR.
  NodeSet getAllReachedUses(RegisterRef RefRR, NodeAddr<DefNode *> DefA) {
    return getAllReachedUses(RefRR, DefA, NoRegs);
  }

  /// Recursively collect reaching definitions of \p RefRR past phi nodes.
  ///
  /// Continues the upward search through phi definitions until real
  /// definitions are found or recursion limits are hit. \p Visited tracks
  /// nodes already explored; \p Defs accumulates definitions found so far.
  /// \param RefRR Register reference whose reaching defs are sought.
  /// \param RefA Reference node at which the recursive search starts.
  /// \param Visited Set of node ids already visited by the recursion.
  /// \param Defs Set of definition node ids collected so far.
  /// \return Pair of the accumulated definition set and a flag that is false
  ///         if recursion nesting exceeded the maximum.
  LLVM_ABI std::pair<NodeSet, bool>
  getAllReachingDefsRec(RegisterRef RefRR, NodeAddr<RefNode *> RefA,
                        NodeSet &Visited, const NodeSet &Defs);

  /// Find the nearest reference aliased to \p RefRR above instruction \p IA.
  ///
  /// Searches upward in program order within the block, then through
  /// immediate dominators, preferring defs over clobbers over uses.
  /// \param RefRR Register reference to match by aliasing.
  /// \param IA Instruction node immediately below the search start.
  /// \return Nearest aliased reference node, or an empty address if none.
  LLVM_ABI NodeAddr<RefNode *> getNearestAliasedRef(RegisterRef RefRR,
                                                    NodeAddr<InstrNode *> IA);

  /// Return a mutable reference to the computed live-in map.
  /// \return Mutable reference to the live-in map.
  LiveMapType &getLiveMap() { return LiveMap; }
  /// Return a const reference to the computed live-in map.
  /// \return Const reference to the live-in map.
  const LiveMapType &getLiveMap() const { return LiveMap; }

  /// Return the map of real (non-phi) uses reached by phi node \p P.
  ///
  /// Returns an empty map if \p P has no recorded real uses.
  /// \param P Node id of the phi whose reached real uses are requested.
  /// \return Const reference to the real-use map for \p P, or an empty map.
  const RefMap &getRealUses(NodeId P) const {
    auto F = RealUseMap.find(P);
    return F == RealUseMap.end() ? Empty : F->second;
  }

  /// Compute the map of real uses reached by each phi definition.
  LLVM_ABI void computePhiInfo();
  /// Compute block live-ins from the data-flow graph and dominance frontier.
  LLVM_ABI void computeLiveIns();
  /// Replace MachineFunction live-ins with the values from the live map.
  LLVM_ABI void resetLiveIns();
  /// Recalculate kill flags for every basic block in the function.
  LLVM_ABI void resetKills();
  /// Recalculate kill flags for basic block \p B.
  /// \param B Basic block whose instruction kill flags are updated.
  LLVM_ABI void resetKills(MachineBasicBlock *B);

  /// Enable or disable tracing output according to \p T.
  /// \param T If true, emit debug tracing during liveness calculation.
  void trace(bool T) { Trace = T; }

private:
  const DataFlowGraph &DFG;
  const TargetRegisterInfo &TRI;
  const PhysicalRegisterInfo &PRI;
  const MachineDominatorTree &MDT;
  const MachineDominanceFrontier &MDF;
  LiveMapType LiveMap;
  const RefMap Empty;
  const RegisterAggr NoRegs;
  bool Trace = false;

  // Cache of mapping from node ids (for RefNodes) to the containing
  // basic blocks. Not computing it each time for each node reduces
  // the liveness calculation time by a large fraction.
  DenseMap<NodeId, MachineBasicBlock *> NBMap;

  // Phi information:
  //
  // RealUseMap
  // map: NodeId -> (map: RegisterId -> NodeRefSet)
  //      phi id -> (map: register -> set of reached non-phi uses)
  DenseMap<NodeId, RefMap> RealUseMap;

  // Inverse iterated dominance frontier.
  std::map<MachineBasicBlock *, std::set<MachineBasicBlock *>> IIDF;

  // Live on entry.
  std::map<MachineBasicBlock *, RefMap> PhiLON;

  // Phi uses are considered to be located at the end of the block that
  // they are associated with. The reaching def of a phi use dominates the
  // block that the use corresponds to, but not the block that contains
  // the phi itself. To include these uses in the liveness propagation (up
  // the dominator tree), create a map: block -> set of uses live on exit.
  std::map<MachineBasicBlock *, RefMap> PhiLOX;

  MachineBasicBlock *getBlockWithRef(NodeId RN) const;
  void traverse(MachineBasicBlock *B, RefMap &LiveIn);
  void emptify(RefMap &M);

  std::pair<NodeSet, bool>
  getAllReachingDefsRecImpl(RegisterRef RefRR, NodeAddr<RefNode *> RefA,
                            NodeSet &Visited, const NodeSet &Defs,
                            unsigned Nest, unsigned MaxNest);
};

/// Write a printed liveness reference map to \p OS.
/// \param OS Output stream.
/// \param P Print wrapper holding the reference map.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const Print<Liveness::RefMap> &P);

} // end namespace llvm::rdf

#endif // LLVM_CODEGEN_RDFLIVENESS_H
