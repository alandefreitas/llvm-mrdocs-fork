//=====-- Rematerializer.h - MIR rematerialization support ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//==-----------------------------------------------------------------------===//
//
/// \file
/// MIR-level target-independent rematerialization helpers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REMATERIALIZER_H
#define LLVM_CODEGEN_REMATERIALIZER_H

#include "llvm/ADT/PointerUnion.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

namespace llvm {

/// MIR-level target-independent rematerializer. Provides an API to identify and
/// rematerialize registers within a machine function.
///
/// At the moment this supports rematerializing registers that meet all of the
/// following constraints.
/// 1. The register is virtual.
/// 2. The register is defined within a single region---potentially over
///    multiple MIs---and isn't used by a MI that is not defining part of the
///    register before its last defining MI. This restriction essentially means
///    that, if the rematerializer only ever rematerializes all the defs of a
///    register together, it can treat all virtual registers as having a "single
///    value" (the one after the last def). Relaxing this restriction would
///    require it to track VNInfos individually rather that virtual registers.
/// 3. All defining instructions are deemed rematerializable by the TII and
///    don't have any physical register use that is both non-constant and
///    non-ignorable.
/// 4. The register has at least one non-debug use that is inside or at a region
///    boundary (see below for what we consider to be a region).
///
/// Rematerializable registers (represented by \ref Rematerializer::Reg) form a
/// DAG of their own, with every register having incoming edges from all
/// rematerializable registers which are read by the instruction defining it. It
/// is possible to rematerialize registers with unrematerializable dependencies;
/// however the latter are not considered part of this DAG since their
/// position/identity never change and therefore do not require the same level
/// of tracking.
///
/// Each register has a "dependency DAG" which is defined as the subset of nodes
/// in the overall DAG that have at least one path to the register, which is
/// called the "root" register in this context. Semantically, these nodes are
/// the registers which are involved into the computation of the root register
/// i.e., all of its transitive dependencies. We use the term "root" because all
/// paths within the dependency DAG of a register terminate at it; however,
/// there may be multiple paths between a non-root node and the root node, so a
/// dependency DAG is not always a tree.
///
/// The API uses dense unsigned integers starting at 0 to reference
/// rematerializable registers. These indices are immutable i.e., even when
/// registers are deleted their respective integer handle remain valid. Method
/// which perform actual rematerializations should however be assumed to
/// invalidate addresses to \ref Rematerializer::Reg objects.
///
/// The rematerializer tracks def/use points of registers based on regions.
/// These are alike the regions the machine scheduler works on. A region is
/// simply a pair on MBB iterators encoding a range of machine instructions. The
/// first iterator (beginning of the region) is inclusive whereas the second
/// iterator (end of the region) is exclusive and can either point to a MBB's
/// end sentinel or an actual MI (not necessarily a terminator). Regions must be
/// non-empty, cannot overlap, and cannot contain terminators. However, they do
/// not have to cover the whole function.
///
/// The API uses dense unsigned integers starting at 0 to reference regions.
/// These map directly to the indices of the corresponding regions in the region
/// vector passed during construction.
///
/// The rematerializer supports rematerializing arbitrary complex DAGs of
/// registers to regions where these registers are used, with the option of
/// re-using non-root registers or their previous rematerializations instead of
/// rematerializing them again.
///
/// Throughout its lifetime, the rematerializer tracks new registers it creates
/// (which are rematerializable by construction) and their relations to other
/// registers. It performs DAG and live interval updates immediately on
/// rematerialization and/or user transfer. Importantly, missing dead flags on
/// partial definitions of unrematerializable registers can yield dead
/// definitions when rematerializing their users. They are deleted to preserve
/// live interval validity. These deletions can cascade to other
/// (un)rematerializable registers that also become dead as a result.
///
/// In its nomenclature, the rematerializer differentiates between "original
/// registers" (registers that were present when it analyzed the function) and
/// rematerializations of these original registers. Rematerializations have an
/// "origin" which is the index of the original register they were
/// rematerialized from (transitivity applies; a rematerialization and all of
/// its own rematerializations have the same origin). Semantically, only
/// original registers have rematerializations.
///
/// Dealing with sub-registers is complicated, we have to handle dead-defs,
/// undef flags, and connected components
class Rematerializer {
public:
  /// Index type for rematerializable registers.
  using RegisterIdx = unsigned;

  /// A rematerializable register, potentially defined by multiple instructions.
  ///
  /// A rematerializable register has a set of dependencies, which correspond
  /// to the unique read register operands of its defining instruction(s) and
  /// which can themselves be rematerializable. Operands of defining
  /// instructions corresponding to unrematerializable dependencies are managed
  /// by and queried from the rematerializer, whereas rematerializable ones are
  /// part of this struct and identified through their register index.
  ///
  /// A rematerializable register also has an arbitrary number of users in an
  /// arbitrary number of regions, potentially including its own defining
  /// region. When rematerializations lead to operand changes in users, a
  /// register may find itself without any user left, at which point the
  /// rematerializer deletes it (emptying \ref Reg::Defs).
  struct Reg {
    /// All instructions that define the register, in program order.
    SmallVector<MachineInstr *, 1> Defs;
    /// Defining region of the register.
    unsigned DefRegion;
    /// The rematerializable register's lane bitmask.
    LaneBitmask Mask;

    /// Set of user instructions of this register within a single region.
    using RegionUsers = SmallDenseSet<MachineInstr *, 4>;
    /// Uses of the register, mapped by region. Users that also define a part of
    /// the register are considered defs and not accounted for here.
    SmallDenseMap<unsigned, RegionUsers, 2> Uses;

    /// This register's rematerializable dependencies, one per unique
    /// rematerializable register operand over all definitions.
    SmallVector<RegisterIdx, 2> Dependencies;

    /// Returns the first defining instruction of this register.
    ///
    /// \return The first instruction in \ref Defs.
    MachineInstr *getFirstDef() const { return Defs.front(); }
    /// Returns the last defining instruction of this register.
    ///
    /// \return The last instruction in \ref Defs.
    MachineInstr *getLastDef() const { return Defs.back(); }

    /// Returns the rematerializable register from one of its defining
    /// instructions.
    ///
    /// \return The virtual register defined by the first defining instruction.
    Register getDefReg() const {
      const MachineInstr *DefMI = getFirstDef();
      assert(DefMI && DefMI->getOperand(0).isDef() && "not a register def");
      return DefMI->getOperand(0).getReg();
    }

    /// Returns whether this register has users in its defining region.
    ///
    /// \return True if there are users in \ref DefRegion.
    bool hasUsersInDefRegion() const {
      return !Uses.empty() && Uses.contains(DefRegion);
    }

    /// Returns whether this register has users outside its defining region.
    ///
    /// \return True if there are users in any region other than \ref DefRegion.
    bool hasUsersOutsideDefRegion() const {
      if (Uses.empty())
        return false;
      return Uses.size() > 1 || Uses.begin()->first != DefRegion;
    }

    /// Returns the index of \p DefMI in the register's definitions order.
    ///
    /// Returns the number of definitions if \p DefMI is not a definition of the
    /// register.
    ///
    /// \param DefMI Defining instruction to look up in \ref Defs.
    /// \return Zero-based index of \p DefMI in \ref Defs, or \ref Defs.size()
    ///         if \p DefMI is not a definition.
    unsigned getDefIdx(MachineInstr *DefMI) const {
      return std::distance(Defs.begin(), find(Defs, DefMI));
    }

    /// Returns the first and last user of the register in region \p UseRegion.
    ///
    /// If the register has no user in the region, returns a pair of nullptr's.
    ///
    /// \param UseRegion Region whose use bounds are queried.
    /// \param LIS Live intervals used to order users within the region.
    /// \return Pair of first and last user in \p UseRegion, or {nullptr,
    ///         nullptr} if there are no users in that region.
    LLVM_ABI std::pair<MachineInstr *, MachineInstr *>
    getRegionUseBounds(unsigned UseRegion, const LiveIntervals &LIS) const;

    /// Returns whether this register still has defining instructions.
    ///
    /// \return True if \ref Defs is non-empty.
    bool isAlive() const { return !Defs.empty(); }

  private:
    void addUser(MachineInstr *MI, unsigned Region);
    void addUsers(const RegionUsers &NewUsers, unsigned Region);
    void eraseUser(MachineInstr *MI, unsigned Region);

    /// Erases user \p MI from region \p Region if it exists. Returns whether \p
    /// MI was actually deleted.
    bool tryEraseUser(MachineInstr *MI, unsigned Region);

    friend Rematerializer;
  };

  /// Rematerializer listener.
  ///
  /// Defines overridable hooks that allow to catch specific events inside the
  /// rematerializer. All hooks do nothing by default. Listeners can be added or
  /// removed at any time during the rematerializer's lifetime.
  class LLVM_ABI Listener {
  public:
    /// Index type for rematerializable registers.
    using RegisterIdx = Rematerializer::RegisterIdx;

    /// Called just after a rematerialized register is created.
    ///
    /// At this point the rematerialization exists in the \p Remater state and
    /// the MIR but does not yet have any user.
    ///
    /// \param Remater Rematerializer that created the register.
    /// \param NewRegIdx Index of the newly created rematerialized register.
    virtual void rematerializerNoteRegCreated(const Rematerializer &Remater,
                                              RegisterIdx NewRegIdx) {}

    /// Called just before register \p RegIdx is deleted from the MIR.
    ///
    /// At this point the register still exists in the MIR but no longer has any
    /// user.
    ///
    /// \param Remater Rematerializer that is deleting the register.
    /// \param RegIdx Index of the register about to be deleted.
    virtual void
    rematerializerNoteRegWillBeDeleted(const Rematerializer &Remater,
                                       RegisterIdx RegIdx) {}

    /// Called just before a dead unrematerializable instruction is deleted.
    ///
    /// \param Remater Rematerializer that is deleting the instruction.
    /// \param MI Unrematerializable instruction about to be deleted because it
    ///        has become a dead definition.
    virtual void
    rematerializerNoteMIWillBeDeleted(const Rematerializer &Remater,
                                      MachineInstr &MI) {}

    /// Destroy this listener.
    virtual ~Listener() = default;

  private:
    virtual void anchor();
  };

  /// Error value for register indices.
  static constexpr unsigned NoReg = ~0;

  /// A region's boundaries i.e. a pair of instruction bundle iterators. The
  /// lower boundary is inclusive, the upper boundary is exclusive.
  using RegionBoundaries =
      std::pair<MachineBasicBlock::iterator, MachineBasicBlock::iterator>;

  /// Set of rematerialization indices of a single original register.
  using RematsOf = SmallDenseSet<RegisterIdx, 4>;

  /// Initializes rematerializer state without identifying candidates.
  ///
  /// Simply initializes some internal state, does not identify
  /// rematerialization candidates.
  ///
  /// \param MF Machine function whose rematerializable registers are tracked.
  /// \param Regions Region boundaries used to track defs and uses.
  /// \param LIS Live intervals for the machine function.
  LLVM_ABI Rematerializer(MachineFunction &MF,
                          SmallVectorImpl<RegionBoundaries> &Regions,
                          LiveIntervals &LIS);

  /// Goes through the whole MF and identifies all rematerializable registers.
  ///
  /// \return True if any rematerializable register was found in the regions.
  LLVM_ABI bool analyze();

  /// Adds a new listener to the rematerializer.
  ///
  /// \param Listen Non-null listener that is not already attached.
  void addListener(Listener *Listen) {
    assert(Listen && "null listener");
    if (!Listeners.insert(Listen).second)
      llvm_unreachable("duplicate listener");
  }

  /// Removes a listener from the rematerializer.
  ///
  /// \param Listen Listener previously added with \ref addListener.
  void removeListener(Listener *Listen) {
    if (!Listeners.erase(Listen))
      llvm_unreachable("unknown listener");
  }

  /// Removes all listeners from the rematerializer.
  void clearListeners() { Listeners.clear(); }

  /// Returns the rematerializable register at index \p RegIdx.
  ///
  /// \param RegIdx Index of the rematerializable register to return.
  /// \return Const reference to the rematerializable register at \p RegIdx.
  const Reg &getReg(RegisterIdx RegIdx) const {
    assert(RegIdx < Regs.size() && "out of bounds");
    return Regs[RegIdx];
  };
  /// Returns all rematerializable registers tracked by this rematerializer.
  ///
  /// \return Array reference over all tracked rematerializable registers.
  ArrayRef<Reg> getRegs() const { return Regs; };
  /// Returns the number of rematerializable registers tracked so far.
  ///
  /// \return Count of rematerializable registers in \ref Regs.
  unsigned getNumRegs() const { return Regs.size(); };

  /// Determines whether register \p RegIdx fully disappeared from the MIR.
  ///
  /// This may happen when it was only used by instructions which became dead
  /// during the rematerializer's lifetime.
  ///
  /// \param RegIdx Register index to test for permanent disappearance.
  /// \return True if the origin of \p RegIdx is dead and has no remats left.
  bool isPermanentlyDead(RegisterIdx RegIdx) const {
    RegisterIdx OrigIdx = getOriginOrSelf(RegIdx);
    return !getReg(OrigIdx).isAlive() && !Rematerializations.contains(OrigIdx);
  }

  /// Returns the boundaries of region \p RegionIdx.
  ///
  /// \param RegionIdx Index of the region whose boundaries are returned.
  /// \return Const reference to the inclusive/exclusive boundaries of the
  ///         region.
  const RegionBoundaries &getRegion(RegisterIdx RegionIdx) const {
    assert(RegionIdx < Regions.size() && "out of bounds");
    return Regions[RegionIdx];
  }
  /// Returns the number of regions passed at construction.
  ///
  /// \return Count of regions in the region vector.
  unsigned getNumRegions() const { return Regions.size(); }

  /// Whether register \p RegIdx is an original register.
  ///
  /// \param RegIdx Register index to classify.
  /// \return True if \p RegIdx is an original register, not a rematerialization.
  bool isOriginalRegister(RegisterIdx RegIdx) const {
    return !isRematerializedRegister(RegIdx);
  }
  /// Whether register \p RegIdx is a rematerialization of some original
  /// register.
  ///
  /// \param RegIdx Register index to classify.
  /// \return True if \p RegIdx is a rematerialization of an original register.
  bool isRematerializedRegister(RegisterIdx RegIdx) const {
    assert(RegIdx < Regs.size() && "out of bounds");
    return RegIdx >= UnrematableDeps.size();
  }
  /// Returns the origin index of rematerializable register \p RematRegIdx.
  ///
  /// \param RematRegIdx Index of a rematerialized register.
  /// \return Index of the original register that \p RematRegIdx rematerializes.
  RegisterIdx getOriginOf(RegisterIdx RematRegIdx) const {
    assert(isRematerializedRegister(RematRegIdx) && "not a rematerialization");
    return Origins[RematRegIdx - UnrematableDeps.size()];
  }
  /// Returns the origin index of \p RegIdx, or \p RegIdx itself if original.
  ///
  /// If \p RegIdx is a rematerialization, returns its origin's index. If it is
  /// an original register's index, returns the same index.
  ///
  /// \param RegIdx Register index whose origin is requested.
  /// \return Origin index of \p RegIdx, or \p RegIdx if it is original.
  RegisterIdx getOriginOrSelf(RegisterIdx RegIdx) const {
    if (isRematerializedRegister(RegIdx))
      return getOriginOf(RegIdx);
    return RegIdx;
  }
  /// Returns unreamaterializable read lanes of register operands for
  /// register \p RegIdx.
  ///
  /// \param RegIdx Register whose unrematerializable dependencies are returned.
  /// \return Unrematerializable (register, lane mask) pairs for the origin of
  ///         \p RegIdx.
  ArrayRef<std::pair<Register, LaneBitmask>>
  getUnrematableDeps(RegisterIdx RegIdx) const {
    return UnrematableDeps[getOriginOrSelf(RegIdx)];
  }

  /// Returns the rematerializable register index defined by \p MI, if any.
  ///
  /// If \p MI's first operand defines a register and that register is a
  /// rematerializable register tracked by the rematerializer, returns its
  /// index in the \ref Regs vector. Otherwise returns \ref
  /// Rematerializer::NoReg.
  ///
  /// \param MI Instruction whose defined rematerializable register is queried.
  /// \return Index of the rematerializable register defined by \p MI, or
  ///         \ref Rematerializer::NoReg if none.
  LLVM_ABI RegisterIdx getDefRegIdx(const MachineInstr &MI) const;

  /// Encodes which rematerializable dependencies to reuse when rematerializing.
  ///
  /// When rematerializating a register (called the "root" register in this
  /// context) to a given position, we must decide what to do with all its
  /// rematerializable dependencies (for unrematerializable dependencies, we
  /// have no choice but to re-use the same register). For each rematerializable
  /// dependency we can either
  /// 1. rematerialize it along with the register,
  /// 2. re-use it as-is, or
  /// 3. re-use a pre-existing rematerialization of it.
  /// In case 1, the same decision needs to be made for all of the dependency's
  /// dependencies. In cases 2 and 3, the dependency's dependencies need not be
  /// examined.
  ///
  /// This struct allows to encode decisions of types (2) and (3) when
  /// rematerialization of all of the root's dependency DAG is undesirable.
  /// During rematerialization, registers in the root's dependency DAG which
  /// have a path to the root made up exclusively of non-re-used registers will
  /// be rematerialized along with the root.
  struct DependencyReuseInfo {
    /// Keys and values are rematerializable register indices.
    ///
    /// Before rematerialization, this only contains entries for non-root
    /// registers of the root's dependency DAG which should not be
    /// rematerialized i.e., for which an existing register should be used
    /// instead. These map each such non-root register to either the same
    /// register (case 2, \ref DependencyReuseInfo::reuse) or to a
    /// rematerialization of the key register (case 3, \ref
    /// DependencyReuseInfo::useRemat).
    ///
    /// After rematerialization, this contains additional entries for non-root
    /// registers of the root's dependency DAG that needed to be rematerialized
    /// along the root. These map each such non-root register to their
    /// corresponding new rematerialization that is used in the rematerialized
    /// root's dependency DAG. It follows that the difference in map size before
    /// and after rematerialization indicates the number of non-root registers
    /// that were rematerialized along the root.
    SmallDenseMap<RegisterIdx, RegisterIdx, 4> DependencyMap;

    /// Records that dependency \p DepIdx should be reused as-is.
    ///
    /// \param DepIdx Rematerializable dependency to reuse without
    ///        rematerializing.
    /// \return Reference to this info for chaining further reuse decisions.
    DependencyReuseInfo &reuse(RegisterIdx DepIdx) {
      DependencyMap.insert({DepIdx, DepIdx});
      return *this;
    }
    /// Records that \p DepIdx should reuse rematerialization \p DepRematIdx.
    ///
    /// \param DepIdx Rematerializable dependency whose remat should be reused.
    /// \param DepRematIdx Existing rematerialization of \p DepIdx to reuse.
    /// \return Reference to this info for chaining further reuse decisions.
    DependencyReuseInfo &useRemat(RegisterIdx DepIdx, RegisterIdx DepRematIdx) {
      DependencyMap.insert({DepIdx, DepRematIdx});
      return *this;
    }
    /// Clears all reuse decisions from this info.
    ///
    /// \return Reference to this info after clearing \ref DependencyMap.
    DependencyReuseInfo &clear() {
      DependencyMap.clear();
      return *this;
    }
  };

  /// Rematerializes \p RootIdx before its first user in \p UseRegion.
  ///
  /// Rematerializes register \p RootIdx just before its first user inside
  /// region \p UseRegion (or at the end of the region if it has no user),
  /// transfers all its users in the region to the new register, and returns the
  /// latter's index. The root's dependency DAG is rematerialized or re-used
  /// according to \p DRI.
  ///
  /// When the method returns, \p DRI contains additional entries for non-root
  /// registers of the root's dependency DAG that needed to be rematerialized
  /// along the root. References to \ref Rematerializer::Reg should be
  /// considered invalidated by calls to this method.
  ///
  /// \param RootIdx Root rematerializable register to rematerialize.
  /// \param UseRegion Region receiving the rematerialization and user transfer.
  /// \param DRI Dependency reuse decisions; updated with rematerialized deps.
  /// \return Index of the newly rematerialized register.
  LLVM_ABI RegisterIdx rematerializeToRegion(RegisterIdx RootIdx,
                                             unsigned UseRegion,
                                             DependencyReuseInfo &DRI);

  /// Rematerializes \p RootIdx before \p InsertPos in \p UseRegion.
  ///
  /// Rematerializes register \p RootIdx before position \p InsertPos in \p
  /// UseRegion and returns the new register's index. The root's dependency DAG
  /// is rematerialized or re-used according to \p DRI.
  ///
  /// When the method returns, \p DRI contains additional entries for non-root
  /// registers of the root's dependency DAG that needed to be rematerialized
  /// along the root. References to \ref Rematerializer::Reg should be
  /// considered invalidated by calls to this method.
  ///
  /// \param RootIdx Root rematerializable register to rematerialize.
  /// \param UseRegion Region containing the insertion point.
  /// \param InsertPos Position before which the rematerialization is inserted.
  /// \param DRI Dependency reuse decisions; updated with rematerialized deps.
  /// \return Index of the newly rematerialized register.
  LLVM_ABI RegisterIdx rematerializeToPos(RegisterIdx RootIdx,
                                          unsigned UseRegion,
                                          MachineBasicBlock::iterator InsertPos,
                                          DependencyReuseInfo &DRI);

  /// Rematerializes \p RegIdx before \p InsertPos without transferring users.
  ///
  /// Rematerializes register \p RegIdx before \p InsertPos in \p UseRegion,
  /// adding the new rematerializable register to the backing vector \ref Regs
  /// and returning its index inside the vector. Sets the new register's
  /// rematerializable dependencies to \p Dependencies (these are assumed to
  /// already exist in the MIR) and its unrematerializable dependencies to the
  /// same as \p RegIdx. The new register initially has no user. Since the
  /// method appends to \ref Regs, references to elements within it should be
  /// considered invalidated across calls to this method unless the vector can
  /// be guaranteed to have enough space for an extra element.
  ///
  /// \param RegIdx Register being rematerialized.
  /// \param UseRegion Region containing the insertion point.
  /// \param InsertPos Position before which the rematerialization is inserted.
  /// \param Dependencies Rematerializable dependencies of the new register.
  /// \return Index of the newly rematerialized register in \ref Regs.
  LLVM_ABI RegisterIdx
  rematerializeReg(RegisterIdx RegIdx, unsigned UseRegion,
                   MachineBasicBlock::iterator InsertPos,
                   SmallVectorImpl<RegisterIdx> &&Dependencies);

  /// Re-creates the defining instructions of a previously deleted register.
  ///
  /// Re-creates each defining instruction of a previously deleted register \p
  /// RegIdx before each position in \p Positions (one position per defining
  /// instruction, in the same order). Positions must be in the same region as
  /// the deleted register, and earlier than all uses of the register in the
  /// region. \p DefReg must be the original virtual register that \p RegIdx
  /// used to define. Rematerializable dependencies are assumed to already exist
  /// in the MIR.
  ///
  /// \param RegIdx Previously deleted rematerializable register to recreate.
  /// \param Positions Insertion points, one per defining instruction in order.
  /// \param DefReg Original virtual register that \p RegIdx used to define.
  LLVM_ABI void recreateReg(RegisterIdx RegIdx,
                            ArrayRef<MachineBasicBlock::iterator> Positions,
                            Register DefReg);

  /// Transfers all users of \p FromRegIdx in \p UseRegion to \p ToRegIdx.
  ///
  /// Transfers all users of register \p FromRegIdx in region \p UseRegion to \p
  /// ToRegIdx, the latter of which must be a rematerialization of the former or
  /// have the same origin register. Users in \p UseRegion must be reachable
  /// from \p ToRegIdx.
  ///
  /// \param FromRegIdx Source register whose regional users are transferred.
  /// \param ToRegIdx Destination rematerialization or same-origin register.
  /// \param UseRegion Region whose users of \p FromRegIdx are transferred.
  LLVM_ABI void transferRegionUsers(RegisterIdx FromRegIdx,
                                    RegisterIdx ToRegIdx, unsigned UseRegion);

  /// Transfers a single user from \p FromRegIdx to \p ToRegIdx.
  ///
  /// Transfers user \p UserMI in region \p UserRegion from register \p
  /// FromRegIdx to \p ToRegIdx, the latter of which must be a rematerialization
  /// of the former or have the same origin register. \p UserMI must be a direct
  /// user of \p FromRegIdx. \p UserMI must be reachable from \p ToRegIdx.
  ///
  /// \param FromRegIdx Source register of the user being transferred.
  /// \param ToRegIdx Destination rematerialization or same-origin register.
  /// \param UserRegion Region containing \p UserMI.
  /// \param UserMI User instruction to transfer to \p ToRegIdx.
  LLVM_ABI void transferUser(RegisterIdx FromRegIdx, RegisterIdx ToRegIdx,
                             unsigned UserRegion, MachineInstr &UserMI);

  /// Transfers all users of \p FromRegIdx to \p ToRegIdx.
  ///
  /// Transfers all users of register \p FromRegIdx to register \p ToRegIdx, the
  /// latter of which must be a rematerialization of the former or have the same
  /// origin register. Users of \p FromRegIdx must be reachable from \p
  /// ToRegIdx.
  ///
  /// \param FromRegIdx Source register whose users are transferred.
  /// \param ToRegIdx Destination rematerialization or same-origin register.
  LLVM_ABI void transferAllUsers(RegisterIdx FromRegIdx, RegisterIdx ToRegIdx);

  /// Determines whether operand \p MO has the same value at all \p Uses.
  ///
  /// Determines whether (sub-)register operand \p MO has the same value at
  /// all \p Uses as at \p MO. This implies that it is also available at all \p
  /// Uses according to its current live interval.
  ///
  /// \param MO Register operand whose value identity is checked.
  /// \param Uses Slot indices at which the operand's value must match.
  /// \return True if \p MO has the same value at all \p Uses as at \p MO.
  LLVM_ABI bool isMOIdenticalAtUses(MachineOperand &MO,
                                    ArrayRef<SlotIndex> Uses) const;

  /// Determines whether lanes of \p Reg match at \p RefSlot and all \p Uses.
  ///
  /// Determines whether lanes \p Mask of register \p Reg habe the same value at
  /// all \p Uses as at \p RefSlot. This implies that it is also available at
  /// all \p Uses according to its current live interval.
  ///
  /// \param Reg Register whose lane values are compared.
  /// \param Mask Lanes of \p Reg to check for identical values.
  /// \param RefSlot Reference slot whose value must match all \p Uses.
  /// \param Uses Slot indices at which the lane values must match \p RefSlot.
  /// \return True if lanes \p Mask of \p Reg match at \p RefSlot and all \p
  ///         Uses.
  LLVM_ABI bool isRegIdenticalAtUses(Register Reg, LaneBitmask Mask,
                                     SlotIndex RefSlot,
                                     ArrayRef<SlotIndex> Uses) const;

  /// Finds the closest rematerialization of \p RegIdx before \p Before.
  ///
  /// Finds the closest rematerialization of register \p RegIdx in region \p
  /// Region that exists before slot \p Before. If no such rematerialization
  /// exists, returns \ref Rematerializer::NoReg.
  ///
  /// \param RegIdx Original or rematerialized register to search remats for.
  /// \param Region Region in which to search for a rematerialization.
  /// \param Before Exclusive upper bound on rematerialization slot indices.
  /// \return Index of the closest rematerialization before \p Before, or
  ///         \ref Rematerializer::NoReg if none exists.
  LLVM_ABI RegisterIdx findRematInRegion(RegisterIdx RegIdx, unsigned Region,
                                         SlotIndex Before) const;

  /// Returns a printable representation of \p RootIdx's dependency DAG.
  ///
  /// \param RootIdx Root register whose dependency DAG is printed.
  /// \return Printable object that formats the dependency DAG of \p RootIdx.
  LLVM_ABI Printable printDependencyDAG(RegisterIdx RootIdx) const;
  /// Returns a printable identifier for rematerializable register \p RegIdx.
  ///
  /// \param RegIdx Register index to format as an identifier.
  /// \return Printable object that formats an identifier for \p RegIdx.
  LLVM_ABI Printable printID(RegisterIdx RegIdx) const;
  /// Returns a printable representation of rematerializable register \p RegIdx.
  ///
  /// \param RegIdx Register to print.
  /// \param SkipRegions If true, omit per-region user information.
  /// \param DefIdx Index of the defining instruction to highlight when
  ///        printing.
  /// \return Printable object that formats rematerializable register \p RegIdx.
  LLVM_ABI Printable printRematReg(RegisterIdx RegIdx, bool SkipRegions = false,
                                   unsigned DefIdx = 0) const;
  /// Returns a printable list of users of rematerializable register \p RegIdx.
  ///
  /// \param RegIdx Register whose users are printed.
  /// \return Printable object that formats the users of \p RegIdx.
  LLVM_ABI Printable printRegUsers(RegisterIdx RegIdx) const;
  /// Returns a printable representation of user instruction \p MI.
  ///
  /// \param MI User instruction to print.
  /// \param UseRegion Optional region that contains the user.
  /// \return Printable object that formats user instruction \p MI.
  LLVM_ABI Printable
  printUser(const MachineInstr *MI,
            std::optional<unsigned> UseRegion = std::nullopt) const;

private:
  struct DeadDefDelegate : LiveRangeEdit::Delegate {
    Rematerializer &Remater;
    DeadDefDelegate(Rematerializer &Remater) : Remater(Remater) {}
    void LRE_WillEraseInstruction(MachineInstr *MI) override;
  };

  SmallVectorImpl<RegionBoundaries> &Regions;
  MachineRegisterInfo &MRI;
  LiveIntervals &LIS;
  const TargetInstrInfo &TII;
  const TargetRegisterInfo &TRI;
  SmallPtrSet<Listener *, 1> Listeners;

  void noteRegCreated(RegisterIdx RegIdx) const {
    for (Listener *Listen : Listeners)
      Listen->rematerializerNoteRegCreated(*this, RegIdx);
  }

  void noteRegWillBeDeleted(RegisterIdx RegIdx) const {
    for (Listener *Listen : Listeners)
      Listen->rematerializerNoteRegWillBeDeleted(*this, RegIdx);
  }

  void noteMIWillBeDeleted(MachineInstr &MI) const {
    for (Listener *Listen : Listeners)
      Listen->rematerializerNoteMIWillBeDeleted(*this, MI);
  }

  /// Rematerializable registers identified since the rematerializer's creation,
  /// both dead and alive, originals and rematerializations. No register is ever
  /// deleted. Indices inside this vector serve as handles for rematerializable
  /// registers.
  SmallVector<Reg> Regs;
  /// For each original register, stores unrematerializable read lanes of
  /// register operands. This doesn't change after the initial collection
  /// period, so the size of the vector indicates the number of original
  /// registers.
  SmallVector<SmallVector<std::pair<Register, LaneBitmask>, 2>> UnrematableDeps;
  /// Indicates the original register index of each rematerialization, in the
  /// order in which they are created. The size of the vector indicates the
  /// total number of rematerializations ever created, including those that were
  /// deleted.
  SmallVector<RegisterIdx> Origins;
  /// Maps original register indices to their currently alive
  /// rematerializations. In practice most registers don't have
  /// rematerializations so this is represented as a map to lower memory cost.
  DenseMap<RegisterIdx, RematsOf> Rematerializations;

  /// Registers mapped to the index of their corresponding rematerialization
  /// data in the \ref Regs vector. This includes registers that no longer exist
  /// in the MIR.
  DenseMap<Register, RegisterIdx> RegToIdx;
  /// Parent block of each region, in order.
  SmallVector<MachineBasicBlock *> RegionMBB;

  /// Common post-processing step after creating a new register \p RematRegIdx
  /// based on register \p ModelRegIdx.
  void postRematerialization(RegisterIdx ModelRegIdx, RegisterIdx RematRegIdx);

  /// Common pre-processing step before deleting a register \p DeleteRegIdx. The
  /// register must still have alive definitions.
  void preDeletion(RegisterIdx DeleteRegIdx);

  /// Extends \p LI over \p Mask to be live at \p UdeIdx.
  void extendInterval(LiveInterval &LI, LaneBitmask Mask,
                      SlotIndex UseIdx) const;

  /// Extends the live interval of rematerializable register \p RegIdx to be
  /// live at the register slot of all MIs in \p NewUsers. Creates and/or
  /// refines the interval's sub-ranges as needed. Updates the register's
  /// defining instruction's dead flag as needed.
  void extendToNewUsers(RegisterIdx RegIdx,
                        ArrayRef<MachineInstr *> NewUsers) const;

  /// Shrinks the live interval of rematerializable register \p RegIdx to its
  /// current uses. If the register has no users, deletes it along with
  /// registers in its dependency DAG that no longer have users as a result.
  void shrinkToUses(RegisterIdx RegIdx);

  /// Shrinks the live interval of unrematerializable register \p Reg to its
  /// current uses. The interval is split if necessary, creating new
  /// unrematerializable registers and updating register dependencies as needed.
  void shrinkToUsesUnremat(Register Reg);

  /// During the analysis phase, creates a \ref Rematerializer::Reg object for
  /// virtual register \p VirtRegIdx if it is rematerializable. \p MIRegion maps
  /// all MIs to their parent region. Set bits in \p SeenRegs indicate virtual
  /// register indices that have already been visited.
  void
  addRegIfRematerializable(unsigned VirtRegIdx,
                           const DenseMap<MachineInstr *, unsigned> &MIRegion,
                           BitVector &SeenRegs);

  /// Determines whether \p MI is considered rematerializable. This further
  /// restricts constraints imposed by the TII on rematerializable instructions,
  /// requiring for example that the defined register is virtual.
  bool isMIRematerializable(const MachineInstr &MI) const;

  /// Implementation of \ref Rematerializer::transferUser that doesn't update
  /// register users.
  void transferUserImpl(RegisterIdx FromRegIdx, RegisterIdx ToRegIdx,
                        MachineInstr &UserMI);

  /// Deletes register \p RootIdx, which must not have any users left. If the
  /// register is deleted, recursively deletes any of its transitive
  /// rematerializable dependencies that no longer have users as a result. In
  /// case of recursive deletion, all of a register's users are always deleted
  /// before the register itself.
  void deleteReg(RegisterIdx RootIdx);
};

/// Rematerializer listener that can recreate registers and roll back remats.
///
/// Rematerializer listener with the ability to re-create deleted registers and
/// rollback rematerializations. Starts recording register deletions and
/// rematerializations as soon as it is attached to the rematerializer.
class LLVM_ABI Rollbacker : public Rematerializer::Listener {
public:
  /// Constructs a rollbacker that is not yet recording events.
  Rollbacker() = default;

  /// Re-creates deleted registers and rolls back recorded rematerializations.
  ///
  /// \param Remater Rematerializer whose recorded changes are rolled back.
  void rollback(Rematerializer &Remater);

  /// Records creation of rematerialized register \p RegIdx.
  ///
  /// \param Remater Rematerializer that created the register.
  /// \param RegIdx Index of the newly created rematerialized register.
  void rematerializerNoteRegCreated(const Rematerializer &Remater,
                                    RegisterIdx RegIdx) override;

  /// Records that register \p RegIdx is about to be deleted.
  ///
  /// \param Remater Rematerializer that is deleting the register.
  /// \param RegIdx Index of the register about to be deleted.
  void rematerializerNoteRegWillBeDeleted(const Rematerializer &Remater,
                                          RegisterIdx RegIdx) override;

  /// Records that dead instruction \p MI is about to be deleted.
  ///
  /// \param Remater Rematerializer that is deleting the instruction.
  /// \param MI Instruction about to be deleted from the MIR.
  void rematerializerNoteMIWillBeDeleted(const Rematerializer &Remater,
                                         MachineInstr &MI) override;

private:
  struct DeadReg {
    /// Register index.
    RegisterIdx Idx;
    /// Original register.
    Register DefReg;
    /// Original definitions of the register. The underlying MIs no longer exist
    /// at rollback time, but may be referenced as re-creation positions for
    /// previously deleted registers.
    SmallVector<MachineInstr *, 1> Defs;

    LLVM_ABI DeadReg(RegisterIdx Idx, const Rematerializer &Remater)
        : Idx(Idx), DefReg(Remater.getReg(Idx).getDefReg()),
          Defs(Remater.getReg(Idx).Defs) {}
  };

  /// An insertion position in the MIR, either a MachineInstr* to insert before
  /// or a MachineBasicBlock* to insert at the end of.
  using InsertBeforePos = PointerUnion<MachineInstr *, MachineBasicBlock *>;

  /// Original registers that have been deleted, in order of deletion.
  SmallVector<DeadReg> DeadRegs;
  /// Re-creation positions for all original registers that have been deleted,
  /// one per defining instruction, in program order for any given register and
  /// in register deletion order overall. A position is either a MachineInstr*
  /// that existed in the MIR at the time the rollbacker was attached to the
  /// rematerializer, or a MachineBasicBlock*.
  SmallVector<InsertBeforePos> Positions;
  /// Maps all re-creation positions that exist in \ref Positions to the indices
  /// of elements holding that position in the vector.
  DenseMap<InsertBeforePos, SmallDenseSet<unsigned, 1>> PosToIdx;
  /// Registers which have been rematerialized (from original index to
  /// rematerialized index).
  DenseMap<RegisterIdx, Rematerializer::RematsOf> Rematerializations;
  /// Used to block further recording of events whenver we are actively rolling
  /// back.
  bool RollingBack = false;

  InsertBeforePos makePos(MachineBasicBlock::iterator It,
                          MachineBasicBlock *MBB) const {
    if (It == MBB->end())
      return InsertBeforePos(MBB);
    return InsertBeforePos(&*It);
  }

  /// Whether \p MI would be deleted if we were to rollback later. These are MIs
  /// defining rematerializable registers whose creation has been recorded by
  /// the rollbacker.
  bool isRollbackableMI(const MachineInstr &MI,
                        const Rematerializer &Remater) const;

  /// Switches all positions that point to \p MI to \p It in the \ref Positions
  /// vector, and updates \ref PosToIdx accordingly. This is used when it
  /// becomes known that \p MI is about to be permanently deleted from the MIR
  /// and thus becomes an invalid re-creation position.
  void invalidatePosition(MachineInstr *MI, MachineBasicBlock::iterator It);
};

} // namespace llvm

#endif // LLVM_CODEGEN_REMATERIALIZER_H
