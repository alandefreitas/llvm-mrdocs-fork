//===- MemorySSA.h - Build Memory SSA ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file exposes an interface to building/using memory SSA to
/// walk memory instructions using a use/def graph.
///
/// Memory SSA class builds an SSA form that links together memory access
/// instructions such as loads, stores, atomics, and calls. Additionally, it
/// does a trivial form of "heap versioning" Every time the memory state changes
/// in the program, we generate a new heap version. It generates
/// MemoryDef/Uses/Phis that are overlayed on top of the existing instructions.
///
/// As a trivial example,
/// define i32 @main() #0 {
/// entry:
///   %call = call noalias i8* @_Znwm(i64 4) #2
///   %0 = bitcast i8* %call to i32*
///   %call1 = call noalias i8* @_Znwm(i64 4) #2
///   %1 = bitcast i8* %call1 to i32*
///   store i32 5, i32* %0, align 4
///   store i32 7, i32* %1, align 4
///   %2 = load i32* %0, align 4
///   %3 = load i32* %1, align 4
///   %add = add nsw i32 %2, %3
///   ret i32 %add
/// }
///
/// Will become
/// define i32 @main() #0 {
/// entry:
///   ; 1 = MemoryDef(0)
///   %call = call noalias i8* @_Znwm(i64 4) #3
///   %2 = bitcast i8* %call to i32*
///   ; 2 = MemoryDef(1)
///   %call1 = call noalias i8* @_Znwm(i64 4) #3
///   %4 = bitcast i8* %call1 to i32*
///   ; 3 = MemoryDef(2)
///   store i32 5, i32* %2, align 4
///   ; 4 = MemoryDef(3)
///   store i32 7, i32* %4, align 4
///   ; MemoryUse(3)
///   %7 = load i32* %2, align 4
///   ; MemoryUse(4)
///   %8 = load i32* %4, align 4
///   %add = add nsw i32 %7, %8
///   ret i32 %add
/// }
///
/// Given this form, all the stores that could ever effect the load at %8 can be
/// gotten by using the MemoryUse associated with it, and walking from use to
/// def until you hit the top of the function.
///
/// Each def also has a list of users associated with it, so you can walk from
/// both def to users, and users to defs. Note that we disambiguate MemoryUses,
/// but not the RHS of MemoryDefs. You can see this above at %7, which would
/// otherwise be a MemoryUse(4). Being disambiguated means that for a given
/// store, all the MemoryUses on its use lists are may-aliases of that store
/// (but the MemoryDefs on its use list may not be).
///
/// MemoryDefs are not disambiguated because it would require multiple reaching
/// definitions, which would require multiple phis, and multiple memoryaccesses
/// per instruction.
///
/// In addition to the def/use graph described above, MemoryDefs also contain
/// an "optimized" definition use.  The "optimized" use points to some def
/// reachable through the memory def chain.  The optimized def *may* (but is
/// not required to) alias the original MemoryDef, but no def *closer* to the
/// source def may alias it.  As the name implies, the purpose of the optimized
/// use is to allow caching of clobber searches for memory defs.  The optimized
/// def may be nullptr, in which case clients must walk the defining access
/// chain.
///
/// When iterating the uses of a MemoryDef, both defining uses and optimized
/// uses will be encountered.  If only one type is needed, the client must
/// filter the use walk.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYSSA_H
#define LLVM_ANALYSIS_MEMORYSSA_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/PHITransAddr.h"
#include "llvm/IR/DerivedUser.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>

namespace llvm {

template <class GraphType> struct GraphTraits;
class Function;
class Loop;
class LLVMContext;
class MemoryAccess;
class MemorySSAWalker;
class Module;
class raw_ostream;

/// Helper tags used to distinguish MemoryAccess intrusive lists.
namespace MSSAHelpers {

/// Tag for the intrusive list of all MemoryAccesses in a block.
struct AllAccessTag {};
/// Tag for the intrusive list of MemoryDefs and MemoryPhis only.
struct DefsOnlyTag {};

} // end namespace MSSAHelpers

/// Sentinel IDs used by MemoryAccess::getID().
enum : unsigned {
  // Used to signify what the default invalid ID is for MemoryAccess's
  // getID()
  /// Invalid MemoryAccess ID used before an access is numbered.
  INVALID_MEMORYACCESS_ID = -1U
};

template <class T> class memoryaccess_def_iterator_base;
/// Mutable iterator over the defining accesses of a MemoryAccess.
using memoryaccess_def_iterator = memoryaccess_def_iterator_base<MemoryAccess>;
/// Const iterator over the defining accesses of a MemoryAccess.
using const_memoryaccess_def_iterator =
    memoryaccess_def_iterator_base<const MemoryAccess>;

/// Base class for MemoryUse, MemoryDef, and MemoryPhi nodes in MemorySSA.
///
/// The base for all memory accesses. All memory accesses in a block are
/// linked together using an intrusive list.
class MemoryAccess
    : public DerivedUser,
      public ilist_node<MemoryAccess, ilist_tag<MSSAHelpers::AllAccessTag>>,
      public ilist_node<MemoryAccess, ilist_tag<MSSAHelpers::DefsOnlyTag>> {
public:
  /// Intrusive-list node type for the all-accesses list.
  using AllAccessType =
      ilist_node<MemoryAccess, ilist_tag<MSSAHelpers::AllAccessTag>>;
  /// Intrusive-list node type for the defs-only list.
  using DefsOnlyType =
      ilist_node<MemoryAccess, ilist_tag<MSSAHelpers::DefsOnlyTag>>;

  /// Deleted copy constructor.
  /// @param Other Unused; copy construction is deleted.
  MemoryAccess(const MemoryAccess &Other) = delete;
  /// Deleted copy assignment.
  /// @param Other Unused; copy assignment is deleted.
  /// @return Reference to this MemoryAccess (deleted).
  MemoryAccess &operator=(const MemoryAccess &Other) = delete;

  /// Deleted; allocate concrete MemoryAccess subclasses instead.
  /// @param Size Unused allocation size.
  void *operator new(size_t Size) = delete;

  // Methods for support type inquiry through isa, cast, and
  // dyn_cast
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param V Value to test.
  /// @return True if \p V is a MemoryAccess.
  static bool classof(const Value *V) {
    unsigned ID = V->getValueID();
    return ID == MemoryUseVal || ID == MemoryPhiVal || ID == MemoryDefVal;
  }

  /// Return the basic block that contains this memory access.
  /// @return The basic block that contains this memory access.
  BasicBlock *getBlock() const { return Block; }

  /// Print this memory access to \p OS.
  /// @param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;
  /// Dump this memory access to stderr for debugging.
  LLVM_ABI void dump() const;

  /// The user iterators for a memory access
  using iterator = user_iterator;
  /// Const user iterator for a memory access.
  using const_iterator = const_user_iterator;

  /// Return a begin iterator over the defining accesses of this MemoryAccess.
  ///
  /// For MemoryPhi nodes, this walks arguments. For MemoryUse/MemoryDef, this
  /// walks the defining access.
  /// @return A begin iterator over the defining accesses of this MemoryAccess.
  memoryaccess_def_iterator defs_begin();
  /// Return a const begin iterator over the defining accesses of this
  /// MemoryAccess.
  /// @return A const begin iterator over the defining accesses of this MemoryAccess.
  const_memoryaccess_def_iterator defs_begin() const;
  /// Return an end iterator over the defining accesses of this MemoryAccess.
  /// @return An end iterator over the defining accesses of this MemoryAccess.
  memoryaccess_def_iterator defs_end();
  /// Return a const end iterator over the defining accesses of this
  /// MemoryAccess.
  /// @return A const end iterator over the defining accesses of this MemoryAccess.
  const_memoryaccess_def_iterator defs_end() const;

  /// Return an iterator to this access in the all-accesses list.
  /// @return An iterator to this access in the all-accesses list.
  AllAccessType::self_iterator getIterator() {
    return this->AllAccessType::getIterator();
  }
  /// Return a const iterator to this access in the all-accesses list.
  /// @return A const iterator to this access in the all-accesses list.
  AllAccessType::const_self_iterator getIterator() const {
    return this->AllAccessType::getIterator();
  }
  /// Return a reverse iterator to this access in the all-accesses list.
  /// @return A reverse iterator to this access in the all-accesses list.
  AllAccessType::reverse_self_iterator getReverseIterator() {
    return this->AllAccessType::getReverseIterator();
  }
  /// Return a const reverse iterator to this access in the all-accesses list.
  /// @return A const reverse iterator to this access in the all-accesses list.
  AllAccessType::const_reverse_self_iterator getReverseIterator() const {
    return this->AllAccessType::getReverseIterator();
  }
  /// Return an iterator to this access in the defs-only list.
  /// @return An iterator to this access in the defs-only list.
  DefsOnlyType::self_iterator getDefsIterator() {
    return this->DefsOnlyType::getIterator();
  }
  /// Return a const iterator to this access in the defs-only list.
  /// @return A const iterator to this access in the defs-only list.
  DefsOnlyType::const_self_iterator getDefsIterator() const {
    return this->DefsOnlyType::getIterator();
  }
  /// Return a reverse iterator to this access in the defs-only list.
  /// @return A reverse iterator to this access in the defs-only list.
  DefsOnlyType::reverse_self_iterator getReverseDefsIterator() {
    return this->DefsOnlyType::getReverseIterator();
  }
  /// Return a const reverse iterator to this access in the defs-only list.
  /// @return A const reverse iterator to this access in the defs-only list.
  DefsOnlyType::const_reverse_self_iterator getReverseDefsIterator() const {
    return this->DefsOnlyType::getReverseIterator();
  }

protected:
  friend class MemoryDef;
  friend class MemoryPhi;
  friend class MemorySSA;
  friend class MemoryUse;
  friend class MemoryUseOrDef;

  /// Used by MemorySSA to change the block of a MemoryAccess when it is
  /// moved.
  /// @param BB New basic block for this access.
  void setBlock(BasicBlock *BB) { Block = BB; }

  /// Return the unique ID of this MemoryDef or MemoryPhi.
  ///
  /// Used for debugging and tracking things about MemoryAccesses.
  /// Guaranteed unique among MemoryAccesses, no guarantees otherwise.
  /// @return Unique ID of this MemoryDef or MemoryPhi.
  inline unsigned getID() const;

  /// Construct a MemoryAccess of subclass \p Vty in block \p BB.
  /// @param C LLVM context.
  /// @param Vty Value subclass ID.
  /// @param DeleteValue Callback used to destroy this derived user.
  /// @param BB Basic block that contains this access.
  /// @param AllocInfo Operand allocation marker.
  MemoryAccess(LLVMContext &C, unsigned Vty, DeleteValueTy DeleteValue,
               BasicBlock *BB, AllocInfo AllocInfo)
      : DerivedUser(Type::getVoidTy(C), Vty, AllocInfo, DeleteValue),
        Block(BB) {}

  // Use deleteValue() to delete a generic MemoryAccess.
  /// Destroy a MemoryAccess; clients should call deleteValue().
  ~MemoryAccess() = default;

private:
  BasicBlock *Block;
};

/// ilist allocation traits that destroy MemoryAccess via deleteValue().
template <>
struct ilist_alloc_traits<MemoryAccess> {
  /// Destroy \p MA through DerivedUser::deleteValue().
  /// @param MA Access node being removed from an intrusive list.
  static void deleteNode(MemoryAccess *MA) { MA->deleteValue(); }
};

/// Write MemoryAccess \p MA to stream \p OS.
/// @param OS Output stream.
/// @param MA Memory access to print.
/// @return Reference to the output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const MemoryAccess &MA) {
  MA.print(OS);
  return OS;
}

/// Common base for MemoryUse and MemoryDef.
///
/// Class that has the common methods + fields of memory uses/defs. It's
/// a little awkward to have, but there are many cases where we want either a
/// use or def, and there are many cases where uses are needed (defs aren't
/// acceptable), and vice-versa.
///
/// This class should never be instantiated directly; make a MemoryUse or
/// MemoryDef instead.
class MemoryUseOrDef : public MemoryAccess {
public:
  /// Deleted; allocate MemoryUse or MemoryDef instead.
  /// @param Size Unused allocation size.
  void *operator new(size_t Size) = delete;

  /// Return operand at index \p i_nocapture.
  /// @param i_nocapture The zero-based operand index.
  /// @return Operand at index \p i_nocapture.
  inline MemoryAccess *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// @param i_nocapture The zero-based operand index.
  /// @param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, MemoryAccess *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return An iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return A const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return An iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return A const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at compile-time index \p Idx.
  /// @return A reference to the operand at compile-time index \p Idx.
  template <int Idx> inline Use &Op();
  /// Return a const reference to the operand at compile-time index \p Idx.
  /// @return A const reference to the operand at compile-time index \p Idx.
  template <int Idx> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The number of operands.
  inline unsigned getNumOperands() const;

  /// Get the instruction that this MemoryUse represents.
  /// @return The instruction that this MemoryUse represents.
  Instruction *getMemoryInst() const { return MemoryInstruction; }

  /// Get the access that produces the memory state used by this Use.
  /// @return The access that produces the memory state used by this Use.
  MemoryAccess *getDefiningAccess() const { return getOperand(0); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param MA Value to test.
  /// @return True if \p MA is a MemoryUse or MemoryDef.
  static bool classof(const Value *MA) {
    return MA->getValueID() == MemoryUseVal || MA->getValueID() == MemoryDefVal;
  }

  /// Do we have an optimized use?
  /// @return True if this use or def has a valid optimized access.
  inline bool isOptimized() const;
  /// Return the MemoryAccess associated with the optimized use, or nullptr.
  /// @return The MemoryAccess associated with the optimized use, or nullptr.
  inline MemoryAccess *getOptimized() const;
  /// Sets the optimized use for a MemoryDef.
  /// @param MA Access to record as the optimized defining access.
  inline void setOptimized(MemoryAccess *MA);

  /// Reset the ID of what this MemoryUse was optimized to, causing it to
  /// be rewalked by the walker if necessary.
  /// This really should only be called by tests.
  inline void resetOptimized();

protected:
  friend class MemorySSA;
  friend class MemorySSAUpdater;

  /// Construct a MemoryUseOrDef for instruction \p MI in block \p BB.
  /// @param C LLVM context.
  /// @param DMA Defining memory access.
  /// @param Vty Value subclass ID (MemoryUseVal or MemoryDefVal).
  /// @param DeleteValue Callback used to destroy this derived user.
  /// @param MI Instruction this access represents.
  /// @param BB Basic block that contains this access.
  /// @param AllocInfo Operand allocation marker.
  MemoryUseOrDef(LLVMContext &C, MemoryAccess *DMA, unsigned Vty,
                 DeleteValueTy DeleteValue, Instruction *MI, BasicBlock *BB,
                 AllocInfo AllocInfo)
      : MemoryAccess(C, Vty, DeleteValue, BB, AllocInfo),
        MemoryInstruction(MI) {
    setDefiningAccess(DMA);
  }

  // Use deleteValue() to delete a generic MemoryUseOrDef.
  /// Destroy a MemoryUseOrDef; clients should call deleteValue().
  ~MemoryUseOrDef() = default;

  /// Set the defining access to \p DMA, optionally as an optimized use.
  /// @param DMA Defining memory access.
  /// @param Optimized Whether \p DMA is also the optimized access.
  void setDefiningAccess(MemoryAccess *DMA, bool Optimized = false) {
    if (!Optimized) {
      setOperand(0, DMA);
      return;
    }
    setOptimized(DMA);
  }

private:
  Instruction *MemoryInstruction;
};

/// Represents read-only accesses to memory
///
/// In particular, the set of Instructions that will be represented by
/// MemoryUse's is exactly the set of Instructions for which
/// AliasAnalysis::getModRefInfo returns "Ref".
class MemoryUse final : public MemoryUseOrDef {
  constexpr static IntrusiveOperandsAllocMarker AllocMarker{1};

public:
  /// Return operand at index \p i_nocapture.
  /// @param i_nocapture The zero-based operand index.
  /// @return Operand at index \p i_nocapture.
  inline MemoryAccess *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// @param i_nocapture The zero-based operand index.
  /// @param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, MemoryAccess *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return An iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return A const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return An iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return A const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at compile-time index \p Idx.
  /// @return A reference to the operand at compile-time index \p Idx.
  template <int Idx> inline Use &Op();
  /// Return a const reference to the operand at compile-time index \p Idx.
  /// @return A const reference to the operand at compile-time index \p Idx.
  template <int Idx> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The number of operands.
  inline unsigned getNumOperands() const;

  /// Construct a MemoryUse for instruction \p MI in block \p BB.
  /// @param C LLVM context.
  /// @param DMA Defining memory access.
  /// @param MI Instruction this use represents.
  /// @param BB Basic block that contains this use.
  MemoryUse(LLVMContext &C, MemoryAccess *DMA, Instruction *MI, BasicBlock *BB)
      : MemoryUseOrDef(C, DMA, MemoryUseVal, deleteMe, MI, BB, AllocMarker) {}

  // allocate space for exactly one operand
  /// Allocate a MemoryUse with space for one operand.
  /// @param S Size requested by the allocator.
  /// @return Pointer to the newly allocated memory.
  void *operator new(size_t S) { return User::operator new(S, AllocMarker); }
  /// Deallocate a MemoryUse created with the fixed-size allocator.
  /// @param Ptr Memory allocated for this MemoryUse.
  void operator delete(void *Ptr) { User::operator delete(Ptr, AllocMarker); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param MA Value to test.
  /// @return True if \p MA is a MemoryUse.
  static bool classof(const Value *MA) {
    return MA->getValueID() == MemoryUseVal;
  }

  /// Print this MemoryUse to \p OS.
  /// @param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Set the optimized defining access to \p DMA.
  /// @param DMA Access to record as both defining and optimized.
  void setOptimized(MemoryAccess *DMA) {
    OptimizedID = DMA->getID();
    setOperand(0, DMA);
  }

  /// Return whether this MemoryUse currently has a valid optimized defining access.
  ///
  /// Whether the MemoryUse is optimized. If ensureOptimizedUses() was called,
  /// uses will usually be optimized, but this is not guaranteed (e.g. due to
  /// invalidation and optimization limits.)
  /// @return True if this MemoryUse currently has a valid optimized defining access.
  bool isOptimized() const {
    return getDefiningAccess() && OptimizedID == getDefiningAccess()->getID();
  }

  /// Return the optimized defining access for this MemoryUse.
  /// @return The optimized defining access for this MemoryUse.
  MemoryAccess *getOptimized() const {
    return getDefiningAccess();
  }

  /// Clear the optimized defining access for this MemoryUse.
  void resetOptimized() {
    OptimizedID = INVALID_MEMORYACCESS_ID;
  }

protected:
  friend class MemorySSA;

private:
  static void deleteMe(DerivedUser *Self);

  unsigned OptimizedID = INVALID_MEMORYACCESS_ID;
};

/// OperandTraits for MemoryUse with a single operand.
template <>
struct OperandTraits<MemoryUse> : public FixedNumOperandTraits<MemoryUse, 1> {};
DEFINE_TRANSPARENT_OPERAND_ACCESSORS(MemoryUse, MemoryAccess)

/// Represents a read-write access to memory, whether it is a must-alias,
/// or a may-alias.
///
/// In particular, the set of Instructions that will be represented by
/// MemoryDef's is exactly the set of Instructions for which
/// AliasAnalysis::getModRefInfo returns "Mod" or "ModRef".
/// Note that, in order to provide def-def chains, all defs also have a use
/// associated with them. This use points to the nearest reaching
/// MemoryDef/MemoryPhi.
class MemoryDef final : public MemoryUseOrDef {
  constexpr static IntrusiveOperandsAllocMarker AllocMarker{2};

public:
  friend class MemorySSA;

  /// Return operand at index \p i_nocapture.
  /// @param i_nocapture The zero-based operand index.
  /// @return Operand at index \p i_nocapture.
  inline MemoryAccess *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// @param i_nocapture The zero-based operand index.
  /// @param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, MemoryAccess *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return An iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return A const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return An iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return A const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at compile-time index \p Idx.
  /// @return A reference to the operand at compile-time index \p Idx.
  template <int Idx> inline Use &Op();
  /// Return a const reference to the operand at compile-time index \p Idx.
  /// @return A const reference to the operand at compile-time index \p Idx.
  template <int Idx> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The number of operands.
  inline unsigned getNumOperands() const;

  /// Construct a MemoryDef for instruction \p MI in block \p BB.
  /// @param C LLVM context.
  /// @param DMA Defining memory access on the use side of this def.
  /// @param MI Instruction this def represents.
  /// @param BB Basic block that contains this def.
  /// @param Ver Unique ID for this def.
  MemoryDef(LLVMContext &C, MemoryAccess *DMA, Instruction *MI, BasicBlock *BB,
            unsigned Ver)
      : MemoryUseOrDef(C, DMA, MemoryDefVal, deleteMe, MI, BB, AllocMarker),
        ID(Ver) {}

  // allocate space for exactly two operands
  /// Allocate a MemoryDef with space for two operands.
  /// @param S Size requested by the allocator.
  /// @return Pointer to the newly allocated memory.
  void *operator new(size_t S) { return User::operator new(S, AllocMarker); }
  /// Deallocate a MemoryDef created with the fixed-size allocator.
  /// @param Ptr Memory allocated for this MemoryDef.
  void operator delete(void *Ptr) { User::operator delete(Ptr, AllocMarker); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param MA Value to test.
  /// @return True if \p MA is a MemoryDef.
  static bool classof(const Value *MA) {
    return MA->getValueID() == MemoryDefVal;
  }

  /// Set the optimized clobbering access to \p MA.
  /// @param MA Access to record as the optimized clobber.
  void setOptimized(MemoryAccess *MA) {
    setOperand(1, MA);
    OptimizedID = MA->getID();
  }

  /// Return the optimized clobbering access, or nullptr if none is set.
  /// @return The optimized clobbering access, or nullptr if none is set.
  MemoryAccess *getOptimized() const {
    return cast_or_null<MemoryAccess>(getOperand(1));
  }

  /// Return true if this MemoryDef has a valid optimized clobbering access.
  /// @return True if this MemoryDef has a valid optimized clobbering access.
  bool isOptimized() const {
    return getOptimized() && OptimizedID == getOptimized()->getID();
  }

  /// Clear the optimized clobbering access for this MemoryDef.
  void resetOptimized() {
    OptimizedID = INVALID_MEMORYACCESS_ID;
    setOperand(1, nullptr);
  }

  /// Print this MemoryDef to \p OS.
  /// @param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Return the unique ID of this MemoryDef.
  /// @return The unique ID of this MemoryDef.
  unsigned getID() const { return ID; }

private:
  static void deleteMe(DerivedUser *Self);

  const unsigned ID;
  unsigned OptimizedID = INVALID_MEMORYACCESS_ID;
};

/// OperandTraits for MemoryDef with two operands.
template <>
struct OperandTraits<MemoryDef> : public FixedNumOperandTraits<MemoryDef, 2> {};
DEFINE_TRANSPARENT_OPERAND_ACCESSORS(MemoryDef, MemoryAccess)

/// OperandTraits that dispatch between MemoryUse and MemoryDef layouts.
template <>
struct OperandTraits<MemoryUseOrDef> {
  /// Return a pointer to the first operand of \p MUD.
  /// @param MUD Use or def whose operands are accessed.
  /// @return A pointer to the first operand of \p MUD.
  static Use *op_begin(MemoryUseOrDef *MUD) {
    if (auto *MU = dyn_cast<MemoryUse>(MUD))
      return OperandTraits<MemoryUse>::op_begin(MU);
    return OperandTraits<MemoryDef>::op_begin(cast<MemoryDef>(MUD));
  }

  /// Return a pointer past the last operand of \p MUD.
  /// @param MUD Use or def whose operands are accessed.
  /// @return A pointer past the last operand of \p MUD.
  static Use *op_end(MemoryUseOrDef *MUD) {
    if (auto *MU = dyn_cast<MemoryUse>(MUD))
      return OperandTraits<MemoryUse>::op_end(MU);
    return OperandTraits<MemoryDef>::op_end(cast<MemoryDef>(MUD));
  }

  /// Return the number of operands of \p MUD.
  /// @param MUD Use or def whose operand count is requested.
  /// @return The number of operands of \p MUD.
  static unsigned operands(const MemoryUseOrDef *MUD) {
    if (const auto *MU = dyn_cast<MemoryUse>(MUD))
      return OperandTraits<MemoryUse>::operands(MU);
    return OperandTraits<MemoryDef>::operands(cast<MemoryDef>(MUD));
  }
};
DEFINE_TRANSPARENT_OPERAND_ACCESSORS(MemoryUseOrDef, MemoryAccess)

/// Represents phi nodes for memory accesses.
///
/// These have the same semantic as regular phi nodes, with the exception that
/// only one phi will ever exist in a given basic block.
/// Guaranteeing one phi per block means guaranteeing there is only ever one
/// valid reaching MemoryDef/MemoryPHI along each path to the phi node.
/// This is ensured by not allowing disambiguation of the RHS of a MemoryDef or
/// a MemoryPhi's operands.
/// That is, given
/// if (a) {
///   store %a
///   store %b
/// }
/// it *must* be transformed into
/// if (a) {
///    1 = MemoryDef(liveOnEntry)
///    store %a
///    2 = MemoryDef(1)
///    store %b
/// }
/// and *not*
/// if (a) {
///    1 = MemoryDef(liveOnEntry)
///    store %a
///    2 = MemoryDef(liveOnEntry)
///    store %b
/// }
/// even if the two stores do not conflict. Otherwise, both 1 and 2 reach the
/// end of the branch, and if there are not two phi nodes, one will be
/// disconnected completely from the SSA graph below that point.
/// Because MemoryUse's do not generate new definitions, they do not have this
/// issue.
class MemoryPhi final : public MemoryAccess {
  constexpr static HungOffOperandsAllocMarker AllocMarker{};

  // allocate space for exactly zero operands
  void *operator new(size_t S) { return User::operator new(S, AllocMarker); }

public:
  /// Deallocate a MemoryPhi created with the hung-off operand allocator.
  /// @param Ptr Memory allocated for this MemoryPhi.
  void operator delete(void *Ptr) { User::operator delete(Ptr, AllocMarker); }

  /// Return operand at index \p i_nocapture.
  /// @param i_nocapture The zero-based operand index.
  /// @return Operand at index \p i_nocapture.
  inline MemoryAccess *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// @param i_nocapture The zero-based operand index.
  /// @param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, MemoryAccess *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return An iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return A const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return An iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return A const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at compile-time index \p Idx.
  /// @return A reference to the operand at compile-time index \p Idx.
  template <int Idx> inline Use &Op();
  /// Return a const reference to the operand at compile-time index \p Idx.
  /// @return A const reference to the operand at compile-time index \p Idx.
  template <int Idx> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The number of operands.
  inline unsigned getNumOperands() const;

  /// Construct a MemoryPhi for block \p BB with version \p Ver.
  /// @param C LLVM context.
  /// @param BB Basic block that owns this phi.
  /// @param Ver Unique ID for this phi.
  /// @param NumPreds Initial reserved predecessor capacity.
  MemoryPhi(LLVMContext &C, BasicBlock *BB, unsigned Ver, unsigned NumPreds = 0)
      : MemoryAccess(C, MemoryPhiVal, deleteMe, BB, AllocMarker), ID(Ver),
        ReservedSpace(NumPreds) {
    allocHungoffUses(ReservedSpace);
  }

  // Block iterator interface. This provides access to the list of incoming
  // basic blocks, which parallels the list of incoming values.
  /// Iterator over the incoming basic blocks of this MemoryPhi.
  using block_iterator = BasicBlock **;
  /// Const iterator over the incoming basic blocks of this MemoryPhi.
  using const_block_iterator = BasicBlock *const *;

  /// Return an iterator to the first incoming basic block.
  /// @return An iterator to the first incoming basic block.
  block_iterator block_begin() {
    return reinterpret_cast<block_iterator>(op_begin() + ReservedSpace);
  }

  /// Return a const iterator to the first incoming basic block.
  /// @return A const iterator to the first incoming basic block.
  const_block_iterator block_begin() const {
    return reinterpret_cast<const_block_iterator>(op_begin() + ReservedSpace);
  }

  /// Return an iterator past the last incoming basic block.
  /// @return An iterator past the last incoming basic block.
  block_iterator block_end() { return block_begin() + getNumOperands(); }

  /// Return a const iterator past the last incoming basic block.
  /// @return A const iterator past the last incoming basic block.
  const_block_iterator block_end() const {
    return block_begin() + getNumOperands();
  }

  /// Return a range over the incoming basic blocks.
  /// @return A range over the incoming basic blocks.
  iterator_range<block_iterator> blocks() {
    return make_range(block_begin(), block_end());
  }

  /// Return a const range over the incoming basic blocks.
  /// @return A const range over the incoming basic blocks.
  iterator_range<const_block_iterator> blocks() const {
    return make_range(block_begin(), block_end());
  }

  /// Return a range over the incoming memory values.
  /// @return A range over the incoming memory values.
  op_range incoming_values() { return operands(); }

  /// Return a const range over the incoming memory values.
  /// @return A const range over the incoming memory values.
  const_op_range incoming_values() const { return operands(); }

  /// Return the number of incoming edges
  /// @return The number of incoming edges.
  unsigned getNumIncomingValues() const { return getNumOperands(); }

  /// Return incoming value number x
  /// @param I Zero-based incoming value index.
  /// @return The incoming value at index \p I.
  MemoryAccess *getIncomingValue(unsigned I) const { return getOperand(I); }
  /// Set incoming value number \p I to \p V.
  /// @param I Zero-based incoming value index.
  /// @param V New incoming memory access.
  void setIncomingValue(unsigned I, MemoryAccess *V) {
    assert(V && "PHI node got a null value!");
    setOperand(I, V);
  }

  /// Map incoming-value index \p I to an operand number.
  /// @param I Incoming value index.
  /// @return The operand number for incoming-value index \p I.
  static unsigned getOperandNumForIncomingValue(unsigned I) { return I; }
  /// Map operand number \p I to an incoming-value index.
  /// @param I Operand number.
  /// @return The incoming-value index for operand number \p I.
  static unsigned getIncomingValueNumForOperand(unsigned I) { return I; }

  /// Return incoming basic block number @p i.
  /// @param I Zero-based incoming block index.
  /// @return The incoming basic block at index \p I.
  BasicBlock *getIncomingBlock(unsigned I) const { return block_begin()[I]; }

  /// Return incoming basic block corresponding
  /// to an operand of the PHI.
  /// @param U Use of an incoming value of this phi.
  /// @return The incoming basic block for operand use \p U.
  BasicBlock *getIncomingBlock(const Use &U) const {
    assert(this == U.getUser() && "Iterator doesn't point to PHI's Uses?");
    return getIncomingBlock(unsigned(&U - op_begin()));
  }

  /// Return incoming basic block corresponding
  /// to value use iterator.
  /// @param I User iterator referring to an incoming value of this phi.
  /// @return The incoming basic block for user iterator \p I.
  BasicBlock *getIncomingBlock(MemoryAccess::const_user_iterator I) const {
    return getIncomingBlock(I.getUse());
  }

  /// Set incoming basic block number \p I to \p BB.
  /// @param I Zero-based incoming block index.
  /// @param BB New incoming basic block.
  void setIncomingBlock(unsigned I, BasicBlock *BB) {
    assert(BB && "PHI node got a null basic block!");
    block_begin()[I] = BB;
  }

  /// Add an incoming value to the end of the PHI list
  /// @param V Incoming memory access.
  /// @param BB Predecessor block associated with \p V.
  void addIncoming(MemoryAccess *V, BasicBlock *BB) {
    if (getNumOperands() == ReservedSpace)
      growOperands(); // Get more space!
    // Initialize some new operands.
    setNumHungOffUseOperands(getNumOperands() + 1);
    setIncomingValue(getNumOperands() - 1, V);
    setIncomingBlock(getNumOperands() - 1, BB);
  }

  /// Return the first index of the specified basic
  /// block in the value list for this PHI.  Returns -1 if no instance.
  /// @param BB Basic block to look up.
  /// @return The first index of \p BB in this PHI, or -1 if absent.
  int getBasicBlockIndex(const BasicBlock *BB) const {
    for (unsigned I = 0, E = getNumOperands(); I != E; ++I)
      if (block_begin()[I] == BB)
        return I;
    return -1;
  }

  /// Return the incoming value associated with predecessor block \p BB.
  /// @param BB Predecessor block to look up.
  /// @return The incoming value associated with predecessor block \p BB.
  MemoryAccess *getIncomingValueForBlock(const BasicBlock *BB) const {
    int Idx = getBasicBlockIndex(BB);
    assert(Idx >= 0 && "Invalid basic block argument!");
    return getIncomingValue(Idx);
  }

  // After deleting incoming position I, the order of incoming may be changed.
  /// Remove incoming edge \p I without preserving order.
  /// @param I Zero-based incoming edge index to remove.
  void unorderedDeleteIncoming(unsigned I) {
    unsigned E = getNumOperands();
    assert(I < E && "Cannot remove out of bounds Phi entry.");
    // MemoryPhi must have at least two incoming values, otherwise the MemoryPhi
    // itself should be deleted.
    assert(E >= 2 && "Cannot only remove incoming values in MemoryPhis with "
                     "at least 2 values.");
    setIncomingValue(I, getIncomingValue(E - 1));
    setIncomingBlock(I, block_begin()[E - 1]);
    setOperand(E - 1, nullptr);
    block_begin()[E - 1] = nullptr;
    setNumHungOffUseOperands(getNumOperands() - 1);
  }

  // After deleting entries that satisfy Pred, remaining entries may have
  // changed order.
  /// Remove every incoming edge for which \p Pred returns true.
  /// @param Pred Predicate of (value, block) pairs to delete.
  template <typename Fn> void unorderedDeleteIncomingIf(Fn &&Pred) {
    for (unsigned I = 0, E = getNumOperands(); I != E; ++I)
      if (Pred(getIncomingValue(I), getIncomingBlock(I))) {
        unorderedDeleteIncoming(I);
        E = getNumOperands();
        --I;
      }
    assert(getNumOperands() >= 1 &&
           "Cannot remove all incoming blocks in a MemoryPhi.");
  }

  // After deleting incoming block BB, the incoming blocks order may be changed.
  /// Remove every incoming edge from basic block \p BB.
  /// @param BB Predecessor block whose edges should be removed.
  void unorderedDeleteIncomingBlock(const BasicBlock *BB) {
    unorderedDeleteIncomingIf(
        [&](const MemoryAccess *, const BasicBlock *B) { return BB == B; });
  }

  // After deleting incoming memory access MA, the incoming accesses order may
  // be changed.
  /// Remove every incoming edge that uses memory access \p MA.
  /// @param MA Incoming memory access whose edges should be removed.
  void unorderedDeleteIncomingValue(const MemoryAccess *MA) {
    unorderedDeleteIncomingIf(
        [&](const MemoryAccess *M, const BasicBlock *) { return MA == M; });
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param V Value to test.
  /// @return True if \p V is a MemoryPhi.
  static bool classof(const Value *V) {
    return V->getValueID() == MemoryPhiVal;
  }

  /// Print this MemoryPhi to \p OS.
  /// @param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Return the unique ID of this MemoryPhi.
  /// @return The unique ID of this MemoryPhi.
  unsigned getID() const { return ID; }

protected:
  friend class MemorySSA;

  /// Allocate hung-off uses for \p N incoming values and their blocks.
  ///
  /// this is more complicated than the generic
  /// User::allocHungoffUses, because we have to allocate Uses for the incoming
  /// values and pointers to the incoming blocks, all in one allocation.
  /// @param N Number of incoming edges to allocate for.
  void allocHungoffUses(unsigned N) {
    User::allocHungoffUses(N, /* IsPhi */ true);
  }

private:
  // For debugging only
  const unsigned ID;
  unsigned ReservedSpace;

  /// This grows the operand list in response to a push_back style of
  /// operation.  This grows the number of ops by 1.5 times.
  void growOperands() {
    unsigned E = getNumOperands();
    // 2 op PHI nodes are VERY common, so reserve at least enough for that.
    ReservedSpace = std::max(E + E / 2, 2u);
    growHungoffUses(ReservedSpace, /* IsPhi */ true);
  }

  static void deleteMe(DerivedUser *Self);
};

/// Return the unique ID of this MemoryDef or MemoryPhi.
/// @return The unique ID of this MemoryDef or MemoryPhi.
inline unsigned MemoryAccess::getID() const {
  assert((isa<MemoryDef>(this) || isa<MemoryPhi>(this)) &&
         "only memory defs and phis have ids");
  if (const auto *MD = dyn_cast<MemoryDef>(this))
    return MD->getID();
  return cast<MemoryPhi>(this)->getID();
}

/// Return whether this use or def currently has a valid optimized access.
/// @return True if this use or def currently has a valid optimized access.
inline bool MemoryUseOrDef::isOptimized() const {
  if (const auto *MD = dyn_cast<MemoryDef>(this))
    return MD->isOptimized();
  return cast<MemoryUse>(this)->isOptimized();
}

/// Return the optimized defining access for this use or def, or nullptr.
/// @return The optimized defining access for this use or def, or nullptr.
inline MemoryAccess *MemoryUseOrDef::getOptimized() const {
  if (const auto *MD = dyn_cast<MemoryDef>(this))
    return MD->getOptimized();
  return cast<MemoryUse>(this)->getOptimized();
}

/// Set the optimized defining access of this use or def to \p MA.
/// @param MA Access to record as the optimized defining access.
inline void MemoryUseOrDef::setOptimized(MemoryAccess *MA) {
  if (auto *MD = dyn_cast<MemoryDef>(this))
    MD->setOptimized(MA);
  else
    cast<MemoryUse>(this)->setOptimized(MA);
}

/// Clear the optimized defining access for this use or def.
inline void MemoryUseOrDef::resetOptimized() {
  if (auto *MD = dyn_cast<MemoryDef>(this))
    MD->resetOptimized();
  else
    cast<MemoryUse>(this)->resetOptimized();
}

/// OperandTraits for MemoryPhi using hung-off operands.
template <> struct OperandTraits<MemoryPhi> : public HungoffOperandTraits {};
DEFINE_TRANSPARENT_OPERAND_ACCESSORS(MemoryPhi, MemoryAccess)

/// Encapsulates MemorySSA, including all data associated with memory
/// accesses.
class MemorySSA {
public:
  /// Build MemorySSA for function \p F.
  /// @param F Function to analyze.
  /// @param AA Alias analysis used while building.
  /// @param DT Dominator tree for \p F.
  LLVM_ABI MemorySSA(Function &F, AliasAnalysis *AA, DominatorTree *DT);
  /// Build MemorySSA for loop \p L.
  /// @param L Loop to analyze.
  /// @param AA Alias analysis used while building.
  /// @param DT Dominator tree for the loop's function.
  LLVM_ABI MemorySSA(Loop &L, AliasAnalysis *AA, DominatorTree *DT);

  // MemorySSA must remain where it's constructed; Walkers it creates store
  // pointers to it.
  /// Deleted; MemorySSA is not movable.
  /// @param Other MemorySSA that would have been moved from.
  MemorySSA(MemorySSA &&Other) = delete;

  /// Destroy this MemorySSA instance and its owned accesses.
  LLVM_ABI ~MemorySSA();

  /// Return the primary MemorySSAWalker for clobber queries.
  /// @return The primary MemorySSAWalker for clobber queries.
  LLVM_ABI MemorySSAWalker *getWalker();
  /// Return a walker that skips the starting access when querying clobbers.
  /// @return A walker that skips the starting access when querying clobbers.
  LLVM_ABI MemorySSAWalker *getSkipSelfWalker();

  /// Return the MemorySSA access associated with instruction \p I.
  ///
  /// Given a memory Mod/Ref'ing instruction, get the MemorySSA
  /// access associated with it. If passed a basic block gets the memory phi
  /// node that exists for that block, if there is one. Otherwise, this will get
  /// a MemoryUseOrDef.
  /// @param I Instruction that Mod/Ref's memory.
  /// @return The MemorySSA access associated with instruction \p I.
  MemoryUseOrDef *getMemoryAccess(const Instruction *I) const {
    return cast_or_null<MemoryUseOrDef>(ValueToMemoryAccess.lookup(I));
  }

  /// Return the MemoryPhi for basic block \p BB, if one exists.
  /// @param BB Basic block that may have a MemoryPhi.
  /// @return The MemoryPhi for basic block \p BB, if one exists.
  MemoryPhi *getMemoryAccess(const BasicBlock *BB) const {
    return cast_or_null<MemoryPhi>(ValueToMemoryAccess.lookup(cast<Value>(BB)));
  }

  /// Return the dominator tree associated with this MemorySSA.
  /// @return The dominator tree associated with this MemorySSA.
  DominatorTree &getDomTree() const { return *DT; }

  /// Dump MemorySSA to stderr for debugging.
  LLVM_ABI void dump() const;
  /// Print MemorySSA to \p OS.
  /// @param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Return true if \p MA represents the live on entry value
  ///
  /// Loads and stores from pointer arguments and other global values may be
  /// defined by memory operations that do not occur in the current function, so
  /// they may be live on entry to the function. MemorySSA represents such
  /// memory state by the live on entry definition, which is guaranteed to occur
  /// before any other memory access in the function.
  /// @param MA Access to test.
  /// @return True if \p MA represents the live on entry value.
  inline bool isLiveOnEntryDef(const MemoryAccess *MA) const {
    return MA == LiveOnEntryDef.get();
  }

  /// Return the live-on-entry MemoryDef for this function or loop.
  /// @return The live-on-entry MemoryDef for this function or loop.
  inline MemoryAccess *getLiveOnEntryDef() const {
    return LiveOnEntryDef.get();
  }

  // Sadly, iplists, by default, owns and deletes pointers added to the
  // list. It's not currently possible to have two iplists for the same type,
  // where one owns the pointers, and one does not. This is because the traits
  // are per-type, not per-tag.  If this ever changes, we should make the
  // DefList an iplist.
  /// Intrusive list of all MemoryAccesses in a block.
  using AccessList = iplist<MemoryAccess, ilist_tag<MSSAHelpers::AllAccessTag>>;
  /// Intrusive list of MemoryDefs and MemoryPhis in a block.
  using DefsList =
      simple_ilist<MemoryAccess, ilist_tag<MSSAHelpers::DefsOnlyTag>>;

  /// Return the list of MemoryAccess's for a given basic block.
  /// @param BB Basic block whose access list is requested.
  /// @return The list of MemoryAccess's for a given basic block.
  AccessList *getBlockAccesses(const BasicBlock *BB) const {
    auto It = PerBlockAccesses.find(BB);
    return It == PerBlockAccesses.end() ? nullptr : It->second.get();
  }

  /// Return the list of MemoryDef's and MemoryPhi's for a given basic
  /// block.
  /// @param BB Basic block whose defs list is requested.
  /// @return The list of MemoryDef's and MemoryPhi's for a given basic block.
  DefsList *getBlockDefs(const BasicBlock *BB) const {
    auto It = PerBlockDefs.find(BB);
    return It == PerBlockDefs.end() ? nullptr : It->second.get();
  }

  /// Given two memory accesses in the same basic block, determine
  /// whether MemoryAccess \p A dominates MemoryAccess \p B.
  /// @param A Candidate dominating access.
  /// @param B Access that may be dominated by \p A.
  /// @return True if \p A locally dominates \p B in the same basic block.
  LLVM_ABI bool locallyDominates(const MemoryAccess *A,
                                 const MemoryAccess *B) const;

  /// Given two memory accesses in potentially different blocks,
  /// determine whether MemoryAccess \p A dominates MemoryAccess \p B.
  /// @param A Candidate dominating access.
  /// @param B Access that may be dominated by \p A.
  /// @return True if \p A dominates \p B.
  LLVM_ABI bool dominates(const MemoryAccess *A, const MemoryAccess *B) const;

  /// Given a MemoryAccess and a Use, determine whether MemoryAccess \p A
  /// dominates Use \p B.
  /// @param A Candidate dominating access.
  /// @param B Use that may be dominated by \p A.
  /// @return True if \p A dominates Use \p B.
  LLVM_ABI bool dominates(const MemoryAccess *A, const Use &B) const;

  /// How thoroughly verifyMemorySSA should check consistency.
  enum class VerificationLevel {
    /// Perform inexpensive consistency checks.
    Fast,
    /// Perform expensive consistency checks.
    Full
  };
  /// Verify that MemorySSA is self consistent (IE definitions dominate
  /// all uses, uses appear in the right places).  This is used by unit tests.
  /// @param VL How thoroughly to verify.
  LLVM_ABI void
      verifyMemorySSA(VerificationLevel VL = VerificationLevel::Fast) const;

  /// Used in various insertion functions to specify whether we are talking
  /// about the beginning or end of a block.
  enum InsertionPlace {
    /// Insert at the beginning of the block.
    Beginning,
    /// Insert at the end of the block.
    End,
    /// Insert before the block terminator.
    BeforeTerminator
  };

  /// Optimize MemoryUses for this MemorySSA instance if not already done.
  ///
  /// By default, uses are *not* optimized during MemorySSA construction.
  /// Calling this method will attempt to optimize all MemoryUses, if this has
  /// not happened yet for this MemorySSA instance. This should be done if you
  /// plan to query the clobbering access for most uses, or if you walk the
  /// def-use chain of uses.
  LLVM_ABI void ensureOptimizedUses();

  /// Return the alias analysis used to build this MemorySSA.
  /// @return The alias analysis used to build this MemorySSA.
  AliasAnalysis &getAA() { return *AA; }

protected:
  // Used by Memory SSA dumpers and wrapper pass
  friend class MemorySSAUpdater;

  /// Verify ordering, domination, and def-use links over \p Blocks.
  /// @param Blocks Blocks to verify.
  /// @param VL How thoroughly to verify.
  template <typename IterT>
  void verifyOrderingDominationAndDefUses(
      IterT Blocks, VerificationLevel VL = VerificationLevel::Fast) const;
  /// Verify per-block domination numbering over \p Blocks.
  /// @param Blocks Blocks to verify.
  template <typename IterT> void verifyDominationNumbers(IterT Blocks) const;
  /// Verify that MemoryPhi previous defs are consistent over \p Blocks.
  /// @param Blocks Blocks to verify.
  template <typename IterT> void verifyPrevDefInPhis(IterT Blocks) const;

  // These is used by the updater to perform various internal MemorySSA
  // machinsations.  They do not always leave the IR in a correct state, and
  // relies on the updater to fixup what it breaks, so it is not public.

  /// Move \p What before iterator \p Where in block \p BB.
  /// @param What Access to move.
  /// @param BB Destination block.
  /// @param Where Insertion point in the access list.
  LLVM_ABI void moveTo(MemoryUseOrDef *What, BasicBlock *BB,
                       AccessList::iterator Where);
  /// Move \p What to insertion place \p Point in block \p BB.
  /// @param What Access to move.
  /// @param BB Destination block.
  /// @param Point Where in the block to insert.
  LLVM_ABI void moveTo(MemoryAccess *What, BasicBlock *BB,
                       InsertionPlace Point);

  // Rename the dominator tree branch rooted at BB.
  /// Rename MemorySSA along the dominator subtree rooted at \p BB.
  /// @param BB Root block of the rename.
  /// @param IncomingVal Incoming memory state at \p BB.
  /// @param Visited Set of already-visited blocks.
  void renamePass(BasicBlock *BB, MemoryAccess *IncomingVal,
                  SmallPtrSetImpl<BasicBlock *> &Visited) {
    renamePass(DT->getNode(BB), IncomingVal, Visited, true, true);
  }

  /// Remove \p MA from value and block lookup maps.
  /// @param MA Access to remove from lookups.
  LLVM_ABI void removeFromLookups(MemoryAccess *MA);
  /// Remove \p MA from block lists, optionally deleting it.
  /// @param MA Access to remove.
  /// @param ShouldDelete Whether to delete \p MA after removal.
  LLVM_ABI void removeFromLists(MemoryAccess *MA, bool ShouldDelete = true);
  /// Insert \p MA into the lists for block \p BB at \p Point.
  /// @param MA Access to insert.
  /// @param BB Block that receives \p MA.
  /// @param Point Where in the block to insert.
  LLVM_ABI void insertIntoListsForBlock(MemoryAccess *MA, const BasicBlock *BB,
                                        InsertionPlace Point);
  /// Insert \p MA into block \p BB's lists before iterator \p It.
  /// @param MA Access to insert.
  /// @param BB Block that receives \p MA.
  /// @param It Insertion point in the access list.
  LLVM_ABI void insertIntoListsBefore(MemoryAccess *MA, const BasicBlock *BB,
                                      AccessList::iterator It);
  /// Create a MemoryUse or MemoryDef for instruction \p I.
  /// @param I Instruction that Mod/Ref's memory.
  /// @param Definition Defining access for the new use or def.
  /// @param Template Optional existing access to copy attributes from.
  /// @param CreationMustSucceed Whether failure to create should assert.
  /// @return The new MemoryUse or MemoryDef for \p I.
  LLVM_ABI MemoryUseOrDef *
  createDefinedAccess(Instruction *I, MemoryAccess *Definition,
                      const MemoryUseOrDef *Template = nullptr,
                      bool CreationMustSucceed = true);

private:
  class ClobberWalkerBase;
  class CachingWalker;
  class SkipSelfWalker;
  class OptimizeUses;

  CachingWalker *getWalkerImpl();
  template <typename IterT>
  void buildMemorySSA(BatchAAResults &BAA, IterT Blocks);

  void prepareForMoveTo(MemoryAccess *, BasicBlock *);
  void verifyUseInDefs(MemoryAccess *, MemoryAccess *) const;

  using AccessMap = DenseMap<const BasicBlock *, std::unique_ptr<AccessList>>;
  using DefsMap = DenseMap<const BasicBlock *, std::unique_ptr<DefsList>>;

  void markUnreachableAsLiveOnEntry(BasicBlock *BB);
  MemoryPhi *createMemoryPhi(BasicBlock *BB);
  template <typename AliasAnalysisType>
  MemoryUseOrDef *createNewAccess(Instruction *, AliasAnalysisType *,
                                  const MemoryUseOrDef *Template = nullptr);
  void placePHINodes(const SmallPtrSetImpl<BasicBlock *> &);
  MemoryAccess *renameBlock(BasicBlock *, MemoryAccess *, bool);
  void renameSuccessorPhis(BasicBlock *, MemoryAccess *, bool);
  LLVM_ABI void renamePass(DomTreeNode *, MemoryAccess *IncomingVal,
                           SmallPtrSetImpl<BasicBlock *> &Visited,
                           bool SkipVisited = false,
                           bool RenameAllUses = false);
  AccessList *getOrCreateAccessList(const BasicBlock *);
  DefsList *getOrCreateDefsList(const BasicBlock *);
  void renumberBlock(const BasicBlock *) const;
  AliasAnalysis *AA = nullptr;
  DominatorTree *DT;
  Function *F = nullptr;
  Loop *L = nullptr;

  // Memory SSA mappings
  DenseMap<const Value *, MemoryAccess *> ValueToMemoryAccess;

  // These two mappings contain the main block to access/def mappings for
  // MemorySSA. The list contained in PerBlockAccesses really owns all the
  // MemoryAccesses.
  // Both maps maintain the invariant that if a block is found in them, the
  // corresponding list is not empty, and if a block is not found in them, the
  // corresponding list is empty.
  AccessMap PerBlockAccesses;
  DefsMap PerBlockDefs;
  std::unique_ptr<MemoryAccess, ValueDeleter> LiveOnEntryDef;

  // Domination mappings
  // Note that the numbering is local to a block, even though the map is
  // global.
  mutable SmallPtrSet<const BasicBlock *, 16> BlockNumberingValid;
  mutable DenseMap<const MemoryAccess *, unsigned long> BlockNumbering;

  // Memory SSA building info
  std::unique_ptr<ClobberWalkerBase> WalkerBase;
  std::unique_ptr<CachingWalker> Walker;
  std::unique_ptr<SkipSelfWalker> SkipWalker;
  unsigned NextID = 0;
  bool IsOptimized = false;
};

/// Enables verification of MemorySSA.
///
/// The checks which this flag enables is exensive and disabled by default
/// unless `EXPENSIVE_CHECKS` is defined.  The flag `-verify-memoryssa` can be
/// used to selectively enable the verification without re-compilation.
LLVM_ABI extern bool VerifyMemorySSA;

/// Internal MemorySSA utilities for MemorySSA classes and walkers.
class MemorySSAUtil {
protected:
  /// Pass allowed to call internal MemorySSA utilities.
  friend class GVNHoist;
  /// Walker allowed to call internal MemorySSA utilities.
  friend class MemorySSAWalker;

  // This function should not be used by new passes.
  /// Return true if MemoryDef \p MD clobbers use-or-def \p MU under \p AA.
  /// @param MD Def that may clobber.
  /// @param MU Use or def being queried.
  /// @param AA Alias analysis used for the query.
  /// @return True if MemoryDef \p MD clobbers use-or-def \p MU under \p AA.
  LLVM_ABI static bool defClobbersUseOrDef(MemoryDef *MD,
                                           const MemoryUseOrDef *MU,
                                           AliasAnalysis &AA);
};

/// An analysis that produces \c MemorySSA for a function.
///
class MemorySSAAnalysis : public AnalysisInfoMixin<MemorySSAAnalysis> {
  friend AnalysisInfoMixin<MemorySSAAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  // Wrap MemorySSA result to ensure address stability of internal MemorySSA
  // pointers after construction.  Use a wrapper class instead of plain
  // unique_ptr<MemorySSA> to avoid build breakage on MSVC.
  /// Analysis result holding a unique_ptr to MemorySSA.
  struct Result {
    /// Construct a result from ownership of \p MSSA.
    /// @param MSSA Newly built MemorySSA instance.
    Result(std::unique_ptr<MemorySSA> &&MSSA) : MSSA(std::move(MSSA)) {}

    /// Return a reference to the owned MemorySSA.
    /// @return A reference to the owned MemorySSA.
    MemorySSA &getMSSA() { return *MSSA; }

    /// Owned MemorySSA instance for the analyzed function.
    std::unique_ptr<MemorySSA> MSSA;

    /// Invalidate this result when analyses preserved by \p PA change.
    /// @param F Function being invalidated.
    /// @param PA Set of preserved analyses.
    /// @param Inv Invalidator for dependent analyses.
    /// @return True if this result is no longer valid.
    LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                             FunctionAnalysisManager::Invalidator &Inv);
  };

  /// Run MemorySSA analysis on function \p F.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing dependencies.
  /// @return MemorySSA analysis result for \p F.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for \c MemorySSA.
class MemorySSAPrinterPass
    : public RequiredPassInfoMixin<MemorySSAPrinterPass> {
  raw_ostream &OS;
  bool EnsureOptimizedUses;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed MemorySSA.
  /// @param EnsureOptimizedUses Whether to optimize uses before printing.
  explicit MemorySSAPrinterPass(raw_ostream &OS, bool EnsureOptimizedUses)
      : OS(OS), EnsureOptimizedUses(EnsureOptimizedUses) {}

  /// Print MemorySSA for \p F and return all analyses preserved.
  /// @param F Function whose MemorySSA is printed.
  /// @param AM Function analysis manager providing MemorySSA.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for \c MemorySSA via the walker.
class MemorySSAWalkerPrinterPass
    : public RequiredPassInfoMixin<MemorySSAWalkerPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a walker printer that writes to \p OS.
  /// @param OS Output stream for the printed walker results.
  explicit MemorySSAWalkerPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print walker-resolved MemorySSA for \p F.
  /// @param F Function whose MemorySSA is printed via the walker.
  /// @param AM Function analysis manager providing MemorySSA.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Verifier pass for \c MemorySSA.
struct MemorySSAVerifierPass : RequiredPassInfoMixin<MemorySSAVerifierPass> {
  /// Verify MemorySSA for \p F and return all analyses preserved.
  /// @param F Function whose MemorySSA is verified.
  /// @param AM Function analysis manager providing MemorySSA.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy analysis pass which computes \c MemorySSA.
class LLVM_ABI MemorySSAWrapperPass : public FunctionPass {
public:
  /// Construct the legacy MemorySSA wrapper pass.
  MemorySSAWrapperPass();

  /// Pass identification, replacement for typeid.
  static char ID;

  /// Compute MemorySSA for \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnFunction(Function &F) override;
  /// Release MemorySSA owned by this pass.
  void releaseMemory() override;
  /// Return the MemorySSA computed by this pass.
  /// @return The MemorySSA computed by this pass.
  MemorySSA &getMSSA() { return *MSSA; }
  /// Return the MemorySSA computed by this pass.
  /// @return The MemorySSA computed by this pass.
  const MemorySSA &getMSSA() const { return *MSSA; }

  /// Report analysis usage for this pass.
  /// @param AU Analysis usage to populate.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Verify the MemorySSA computed by this pass.
  void verifyAnalysis() const override;
  /// Print the MemorySSA computed by this pass.
  /// @param OS Output stream.
  /// @param M Optional module (unused).
  void print(raw_ostream &OS, const Module *M = nullptr) const override;

private:
  std::unique_ptr<MemorySSA> MSSA;
};

/// Generic walker interface for further disambiguating MemorySSA def-use chains.
///
/// Walkers are used to be able to further disambiguate the def-use chains
/// MemorySSA gives you, or otherwise produce better info than MemorySSA gives
/// you.
/// In particular, while the def-use chains provide basic information, and are
/// guaranteed to give, for example, the nearest may-aliasing MemoryDef for a
/// MemoryUse as AliasAnalysis considers it, a user mant want better or other
/// information. In particular, they may want to use SCEV info to further
/// disambiguate memory accesses, or they may want the nearest dominating
/// may-aliasing MemoryDef for a call or a store. This API enables a
/// standardized interface to getting and using that info.
class MemorySSAWalker {
public:
  /// Construct a walker bound to MemorySSA instance \p MSSA.
  /// @param MSSA MemorySSA instance this walker queries.
  LLVM_ABI MemorySSAWalker(MemorySSA *MSSA);
  /// Destroy this walker.
  virtual ~MemorySSAWalker() = default;

  /// Small vector of MemoryAccess pointers returned by some walker queries.
  using MemoryAccessSet = SmallVector<MemoryAccess *, 8>;

  /// Return the nearest dominating MemoryAccess that Mod's the location of \p I.
  ///
  /// Given a memory Mod/Ref/ModRef'ing instruction, calling this
  /// will give you the nearest dominating MemoryAccess that Mod's the location
  /// the instruction accesses (by skipping any def which AA can prove does not
  /// alias the location(s) accessed by the instruction given).
  ///
  /// Note that this will return a single access, and it must dominate the
  /// Instruction, so if an operand of a MemoryPhi node Mod's the instruction,
  /// this will return the MemoryPhi, not the operand. This means that
  /// given:
  /// if (a) {
  ///   1 = MemoryDef(liveOnEntry)
  ///   store %a
  /// } else {
  ///   2 = MemoryDef(liveOnEntry)
  ///   store %b
  /// }
  /// 3 = MemoryPhi(2, 1)
  /// MemoryUse(3)
  /// load %a
  ///
  /// calling this API on load(%a) will return the MemoryPhi, not the MemoryDef
  /// in the if (a) branch.
  /// @param I Instruction whose accessed location is queried.
  /// @param AA Batch alias analysis used to skip non-aliasing defs.
  /// @return The nearest dominating MemoryAccess that Mod's the location of \p I.
  MemoryAccess *getClobberingMemoryAccess(const Instruction *I,
                                          BatchAAResults &AA) {
    MemoryAccess *MA = MSSA->getMemoryAccess(I);
    assert(MA && "Handed an instruction that MemorySSA doesn't recognize?");
    return getClobberingMemoryAccess(MA, AA);
  }

  /// Return the nearest dominating clobbering access for MemoryAccess \p MA.
  ///
  /// Does the same thing as getClobberingMemoryAccess(const Instruction *I),
  /// but takes a MemoryAccess instead of an Instruction.
  /// @param MA Memory access whose clobber is queried.
  /// @param AA Batch alias analysis used to skip non-aliasing defs.
  /// @return The nearest dominating clobbering access for MemoryAccess \p MA.
  virtual MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                                  BatchAAResults &AA) = 0;

  /// Return the nearest dominating clobber of \p MA for location \p Loc.
  ///
  /// Given a potentially clobbering memory access and a new location,
  /// calling this will give you the nearest dominating clobbering MemoryAccess
  /// (by skipping non-aliasing def links).
  ///
  /// This version of the function is mainly used to disambiguate phi translated
  /// pointers, where the value of a pointer may have changed from the initial
  /// memory access. Note that this expects to be handed a potentially
  /// clobbering access (either a MemoryDef or a MemoryPhi). Unlike the above
  /// API, if given a MemoryDef that clobbers the pointer as the starting
  /// access, it will return that MemoryDef, whereas the above would return the
  /// clobber starting from the use side of the memory def.
  /// @param MA Potentially clobbering starting access.
  /// @param Loc Memory location to query after phi translation.
  /// @param AA Batch alias analysis used to skip non-aliasing defs.
  /// @return The nearest dominating clobber of \p MA for location \p Loc.
  virtual MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                                  const MemoryLocation &Loc,
                                                  BatchAAResults &AA) = 0;

  /// Return the nearest dominating clobbering access for instruction \p I.
  /// @param I Instruction whose accessed location is queried.
  /// @return The nearest dominating clobbering access for instruction \p I.
  MemoryAccess *getClobberingMemoryAccess(const Instruction *I) {
    BatchAAResults BAA(MSSA->getAA());
    return getClobberingMemoryAccess(I, BAA);
  }

  /// Return the nearest dominating clobbering access for MemoryAccess \p MA.
  /// @param MA Memory access whose clobber is queried.
  /// @return The nearest dominating clobbering access for MemoryAccess \p MA.
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA) {
    BatchAAResults BAA(MSSA->getAA());
    return getClobberingMemoryAccess(MA, BAA);
  }

  /// Return the nearest dominating clobber of \p MA for location \p Loc.
  /// @param MA Potentially clobbering starting access.
  /// @param Loc Memory location to query.
  /// @return The nearest dominating clobber of \p MA for location \p Loc.
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          const MemoryLocation &Loc) {
    BatchAAResults BAA(MSSA->getAA());
    return getClobberingMemoryAccess(MA, Loc, BAA);
  }

  /// Invalidate cached walker info for memory access \p MA.
  ///
  /// This API is used by walkers that store information to perform basic cache
  /// invalidation.  This will be called by MemorySSA at appropriate times for
  /// the walker it uses or returns.
  /// @param MA Access whose cached info should be discarded.
  virtual void invalidateInfo(MemoryAccess *MA) {}

protected:
  friend class MemorySSA; // For updating MSSA pointer in MemorySSA move
                          // constructor.
  /// MemorySSA instance this walker is bound to.
  MemorySSA *MSSA;
};

/// MemorySSAWalker that returns builder links without alias queries.
///
/// A MemorySSAWalker that does no alias queries, or anything else. It
/// simply returns the links as they were constructed by the builder.
class LLVM_ABI DoNothingMemorySSAWalker final : public MemorySSAWalker {
public:
  // Keep the overrides below from hiding the Instruction overload of
  // getClobberingMemoryAccess.
  /// Bring base-class Instruction overloads into scope.
  using MemorySSAWalker::getClobberingMemoryAccess;

  /// Return the defining access of \p MA without performing alias queries.
  /// @param MA Memory access whose defining link is returned.
  /// @param AA Unused; accepted to match the walker interface.
  /// @return The defining access of \p MA without performing alias queries.
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          BatchAAResults &AA) override;
  /// Return the defining access of \p MA without performing alias queries.
  /// @param MA Memory access whose defining link is returned.
  /// @param Loc Unused; accepted to match the walker interface.
  /// @param AA Unused; accepted to match the walker interface.
  /// @return The defining access of \p MA without performing alias queries.
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          const MemoryLocation &Loc,
                                          BatchAAResults &AA) override;
};

/// Iterator over the defining accesses of a MemoryAccess.
///
/// Implements const and non-const iterators over the defining accesses of a
/// MemoryAccess.
template <class T>
class memoryaccess_def_iterator_base
    : public iterator_facade_base<memoryaccess_def_iterator_base<T>,
                                  std::forward_iterator_tag, T, ptrdiff_t, T *,
                                  T *> {
  using BaseT = typename memoryaccess_def_iterator_base::iterator_facade_base;

public:
  /// Construct a def iterator starting at \p Start.
  /// @param Start Memory access whose defining accesses to walk.
  memoryaccess_def_iterator_base(T *Start) : Access(Start) {}
  /// Construct an end (singular) def iterator.
  memoryaccess_def_iterator_base() = default;

  /// Return true if this iterator is at the same position as \p Other.
  /// @param Other Other def iterator to compare against.
  /// @return True if this iterator is at the same position as \p Other.
  bool operator==(const memoryaccess_def_iterator_base &Other) const {
    return Access == Other.Access && (!Access || ArgNo == Other.ArgNo);
  }

  // This is a bit ugly, but for MemoryPHI's, unlike PHINodes, you can't get the
  // block from the operand in constant time (In a PHINode, the uselist has
  // both, so it's just subtraction). We provide it as part of the
  // iterator to avoid callers having to linear walk to get the block.
  // If the operation becomes constant time on MemoryPHI's, this bit of
  // abstraction breaking should be removed.
  /// Return the incoming basic block for the current MemoryPhi argument.
  /// @return The incoming basic block for the current MemoryPhi argument.
  BasicBlock *getPhiArgBlock() const {
    MemoryPhi *MP = dyn_cast<MemoryPhi>(Access);
    assert(MP && "Tried to get phi arg block when not iterating over a PHI");
    return MP->getIncomingBlock(ArgNo);
  }

  /// Return the current defining access (or MemoryPhi incoming value).
  /// @return The current defining access (or MemoryPhi incoming value).
  typename std::iterator_traits<BaseT>::pointer operator*() const {
    assert(Access && "Tried to access past the end of our iterator");
    // Go to the first argument for phis, and the defining access for everything
    // else.
    if (const MemoryPhi *MP = dyn_cast<MemoryPhi>(Access))
      return MP->getIncomingValue(ArgNo);
    return cast<MemoryUseOrDef>(Access)->getDefiningAccess();
  }

  /// Inherit the postfix increment from the facade base.
  using BaseT::operator++;
  /// Advance to the next defining access or MemoryPhi argument.
  /// @return Reference to this iterator.
  memoryaccess_def_iterator_base &operator++() {
    assert(Access && "Hit end of iterator");
    if (const MemoryPhi *MP = dyn_cast<MemoryPhi>(Access)) {
      if (++ArgNo >= MP->getNumIncomingValues()) {
        ArgNo = 0;
        Access = nullptr;
      }
    } else {
      Access = nullptr;
    }
    return *this;
  }

private:
  T *Access = nullptr;
  unsigned ArgNo = 0;
};

/// Return a begin iterator over the defining accesses of this MemoryAccess.
///
/// For MemoryPhi nodes, this walks arguments. For MemoryUse/MemoryDef, this
/// walks the defining access.
/// @return A begin iterator over the defining accesses of this MemoryAccess.
inline memoryaccess_def_iterator MemoryAccess::defs_begin() {
  return memoryaccess_def_iterator(this);
}

/// Return a const begin iterator over the defining accesses of this MemoryAccess.
/// @return A const begin iterator over the defining accesses of this MemoryAccess.
inline const_memoryaccess_def_iterator MemoryAccess::defs_begin() const {
  return const_memoryaccess_def_iterator(this);
}

/// Return an end iterator over the defining accesses of this MemoryAccess.
/// @return An end iterator over the defining accesses of this MemoryAccess.
inline memoryaccess_def_iterator MemoryAccess::defs_end() {
  return memoryaccess_def_iterator();
}

/// Return a const end iterator over the defining accesses of this MemoryAccess.
/// @return A const end iterator over the defining accesses of this MemoryAccess.
inline const_memoryaccess_def_iterator MemoryAccess::defs_end() const {
  return const_memoryaccess_def_iterator();
}

/// GraphTraits specialization that walks defining accesses of a MemoryAccess.
template <> struct GraphTraits<MemoryAccess *> {
  /// Graph node type for a MemoryAccess.
  using NodeRef = MemoryAccess *;
  /// Iterator over defining-access children.
  using ChildIteratorType = memoryaccess_def_iterator;

  /// Return \p N as the graph entry node.
  /// @param N Memory access used as the entry.
  /// @return The graph entry node \p N.
  static NodeRef getEntryNode(NodeRef N) { return N; }
  /// Return the begin iterator over defining accesses of \p N.
  /// @param N Memory access whose defs to walk.
  /// @return The begin iterator over defining accesses of \p N.
  static ChildIteratorType child_begin(NodeRef N) { return N->defs_begin(); }
  /// Return the end iterator over defining accesses of \p N.
  /// @param N Memory access whose defs to walk.
  /// @return The end iterator over defining accesses of \p N.
  static ChildIteratorType child_end(NodeRef N) { return N->defs_end(); }
};

/// Inverse GraphTraits specialization that walks MemoryAccess users.
template <> struct GraphTraits<Inverse<MemoryAccess *>> {
  /// Graph node type for a MemoryAccess.
  using NodeRef = MemoryAccess *;
  /// Iterator over user children.
  using ChildIteratorType = MemoryAccess::iterator;

  /// Return \p N as the graph entry node.
  /// @param N Memory access used as the entry.
  /// @return The graph entry node \p N.
  static NodeRef getEntryNode(NodeRef N) { return N; }
  /// Return the begin iterator over users of \p N.
  /// @param N Memory access whose users to walk.
  /// @return The begin iterator over users of \p N.
  static ChildIteratorType child_begin(NodeRef N) { return N->user_begin(); }
  /// Return the end iterator over users of \p N.
  /// @param N Memory access whose users to walk.
  /// @return The end iterator over users of \p N.
  static ChildIteratorType child_end(NodeRef N) { return N->user_end(); }
};

/// Pair of a memory access and the location being queried while walking up.
struct UpwardDefsElem {
  /// Current memory access in the upward walk.
  MemoryAccess *MA;
  /// Memory location associated with \c MA, possibly after phi translation.
  MemoryLocation Loc;
  /// True if the location may carry a cross-iteration dependence.
  bool MayBeCrossIteration;
};

/// DenseMapInfo for \c UpwardDefsElem keyed by access, location, and flag.
template <> struct DenseMapInfo<UpwardDefsElem> {
  /// Return a hash of \p Val's access, location, and cross-iteration flag.
  /// @param Val Element to hash.
  /// @return A hash of \p Val's access, location, and cross-iteration flag.
  static unsigned getHashValue(const UpwardDefsElem &Val) {
    return hash_combine(DenseMapInfo<MemoryAccess *>::getHashValue(Val.MA),
                        DenseMapInfo<MemoryLocation>::getHashValue(Val.Loc),
                        Val.MayBeCrossIteration);
  }

  /// Return true if \p LHS and \p RHS compare equal.
  /// @param LHS First element.
  /// @param RHS Second element.
  /// @return True if \p LHS and \p RHS compare equal.
  static bool isEqual(const UpwardDefsElem &LHS, const UpwardDefsElem &RHS) {
    return LHS.MA == RHS.MA && LHS.Loc == RHS.Loc &&
           LHS.MayBeCrossIteration == RHS.MayBeCrossIteration;
  }
};

/// Iterator that walks defs with a phi-translated memory location.
///
/// Gives both the memory access and the current pointer location, updating the
/// pointer location as it changes due to phi node translation.
///
/// This iterator, while somewhat specialized, is what most clients actually
/// want when walking upwards through MemorySSA def chains. It takes a pair of
/// <MemoryAccess,MemoryLocation>, and walks defs, properly translating the
/// memory location through phi nodes for the user.
class upward_defs_iterator
    : public iterator_facade_base<upward_defs_iterator,
                                  std::forward_iterator_tag,
                                  const UpwardDefsElem> {
  using BaseT = upward_defs_iterator::iterator_facade_base;

public:
  /// Construct an upward-defs iterator from \p Elem using dominator tree \p DT.
  /// @param Elem Starting access, location, and cross-iteration flag.
  /// @param DT Dominator tree used for phi translation.
  upward_defs_iterator(const UpwardDefsElem &Elem, DominatorTree *DT)
      : DefIterator(Elem.MA), Location(Elem.Loc), OriginalAccess(Elem.MA),
        DT(DT) {
    CurrentElem.MA = nullptr;
    CurrentElem.MayBeCrossIteration = Elem.MayBeCrossIteration;

    WalkingPhi = Elem.MA && isa<MemoryPhi>(Elem.MA);
    fillInCurrentElem();
  }

  /// Construct an end (singular) upward-defs iterator.
  upward_defs_iterator() { CurrentElem.MA = nullptr; }

  /// Return true if this iterator is at the same position as \p Other.
  /// @param Other Other upward-defs iterator to compare against.
  /// @return True if this iterator is at the same position as \p Other.
  bool operator==(const upward_defs_iterator &Other) const {
    return DefIterator == Other.DefIterator;
  }

  /// Return the current access and translated memory location.
  /// @return The current access and translated memory location.
  std::iterator_traits<BaseT>::reference operator*() const {
    assert(DefIterator != OriginalAccess->defs_end() &&
           "Tried to access past the end of our iterator");
    return CurrentElem;
  }

  /// Inherit the postfix increment from the facade base.
  using BaseT::operator++;
  /// Advance to the next defining access, updating the translated location.
  /// @return Reference to this iterator.
  upward_defs_iterator &operator++() {
    assert(DefIterator != OriginalAccess->defs_end() &&
           "Tried to access past the end of the iterator");
    ++DefIterator;
    if (DefIterator != OriginalAccess->defs_end())
      fillInCurrentElem();
    return *this;
  }

  /// Return the incoming basic block for the current MemoryPhi argument.
  /// @return The incoming basic block for the current MemoryPhi argument.
  BasicBlock *getPhiArgBlock() const { return DefIterator.getPhiArgBlock(); }

private:
  /// Returns true if \p Ptr is guaranteed to be loop invariant for any possible
  /// loop. In particular, this guarantees that it only references a single
  /// MemoryLocation during execution of the containing function.
  /// @param Ptr Pointer value to test for loop invariance.
  /// @return True if \p Ptr is guaranteed to be loop invariant.
  LLVM_ABI bool IsGuaranteedLoopInvariant(const Value *Ptr) const;

  void fillInCurrentElem() {
    CurrentElem.MA = *DefIterator;
    CurrentElem.Loc = Location;
    // No need for phi translation or handling of cross-iteration dependences
    // if we're not walking past a phi.
    if (!WalkingPhi)
      return;

    if (Location.Ptr) {
      PHITransAddr Translator(
          const_cast<Value *>(Location.Ptr),
          OriginalAccess->getBlock()->getDataLayout(), nullptr);

      if (Value *Addr =
              Translator.translateValue(OriginalAccess->getBlock(),
                                        DefIterator.getPhiArgBlock(), DT, true))
        if (Addr != CurrentElem.Loc.Ptr)
          CurrentElem.Loc = CurrentElem.Loc.getWithNewPtr(Addr);

      // Mark size as unknown, if the location is not guaranteed to be
      // loop-invariant for any possible loop in the function. Setting the size
      // to unknown guarantees that any memory accesses that access locations
      // after the pointer are considered as clobbers, which is important to
      // catch loop carried dependences.
      if (!IsGuaranteedLoopInvariant(CurrentElem.Loc.Ptr))
        // TODO: We should be using MayBeCrossIteration here as well.
        CurrentElem.Loc = CurrentElem.Loc.getWithNewSize(
            LocationSize::beforeOrAfterPointer());
    } else {
      // We can't easily analyze invariance for calls, so conservatively assume
      // they may be introducing cross-iteration dependences for any phi
      // translation.
      CurrentElem.MayBeCrossIteration = true;
    }
  }

  UpwardDefsElem CurrentElem;
  memoryaccess_def_iterator DefIterator;
  MemoryLocation Location;
  MemoryAccess *OriginalAccess = nullptr;
  DominatorTree *DT = nullptr;
  bool WalkingPhi = false;
};

/// Return the begin iterator for an upward defs walk of \p Pair.
/// @param Pair Starting access, location, and cross-iteration flag.
/// @param DT Dominator tree used for phi translation.
/// @return The begin iterator for an upward defs walk of \p Pair.
inline upward_defs_iterator upward_defs_begin(const UpwardDefsElem &Pair,
                                              DominatorTree &DT) {
  return upward_defs_iterator(Pair, &DT);
}

/// Return the end iterator for an upward defs walk.
/// @return The end iterator for an upward defs walk.
inline upward_defs_iterator upward_defs_end() { return upward_defs_iterator(); }

/// Return a range walking upward defs from \p Pair with phi translation.
/// @param Pair Starting access, location, and cross-iteration flag.
/// @param DT Dominator tree used for phi translation.
/// @return A range walking upward defs from \p Pair with phi translation.
inline iterator_range<upward_defs_iterator>
upward_defs(const UpwardDefsElem &Pair, DominatorTree &DT) {
  return make_range(upward_defs_begin(Pair, DT), upward_defs_end());
}

/// Forward iterator over the defining-access chain of a MemoryDef or MemoryUse.
///
/// Stops after we hit something that has no defining use (e.g. a MemoryPhi or
/// liveOnEntry). Note that, when comparing against a null def_chain_iterator,
/// this will compare equal only after walking said Phi/liveOnEntry.
///
/// The UseOptimizedChain flag specifies whether to walk the clobbering
/// access chain, or all the accesses.
///
/// Normally, MemoryDef are all just def/use linked together, so a def_chain on
/// a MemoryDef will walk all MemoryDefs above it in the program until it hits
/// a phi node.  The optimized chain walks the clobbering access of a store.
/// So if you are just trying to find, given a store, what the next
/// thing that would clobber the same memory is, you want the optimized chain.
template <class T, bool UseOptimizedChain = false>
struct def_chain_iterator
    : public iterator_facade_base<def_chain_iterator<T, UseOptimizedChain>,
                                  std::forward_iterator_tag, MemoryAccess *> {
  /// Construct an end (null) def-chain iterator.
  def_chain_iterator() : MA(nullptr) {}
  /// Construct a def-chain iterator starting at \p MA.
  /// @param MA Access at which to begin walking defining accesses.
  def_chain_iterator(T MA) : MA(MA) {}

  /// Return the current memory access in the chain.
  /// @return The current memory access in the chain.
  T operator*() const { return MA; }

  /// Advance to the next defining access (or optimized clobber if enabled).
  /// @return Reference to this iterator.
  def_chain_iterator &operator++() {
    // N.B. liveOnEntry has a null defining access.
    if (auto *MUD = dyn_cast<MemoryUseOrDef>(MA)) {
      if (UseOptimizedChain && MUD->isOptimized())
        MA = MUD->getOptimized();
      else
        MA = MUD->getDefiningAccess();
    } else {
      MA = nullptr;
    }

    return *this;
  }

  /// Return true if this iterator refers to the same access as \p O.
  /// @param O Other def-chain iterator to compare against.
  /// @return True if this iterator refers to the same access as \p O.
  bool operator==(const def_chain_iterator &O) const { return MA == O.MA; }

private:
  T MA;
};

/// Return a range walking defining accesses from \p MA up to \p UpTo.
/// @param MA Access at which to begin the def chain.
/// @param UpTo Optional end access; the range stops before this node.
/// @return A range walking defining accesses from \p MA up to \p UpTo.
template <class T>
inline iterator_range<def_chain_iterator<T>>
def_chain(T MA, MemoryAccess *UpTo = nullptr) {
#ifdef EXPENSIVE_CHECKS
  assert((!UpTo || find(def_chain(MA), UpTo) != def_chain_iterator<T>()) &&
         "UpTo isn't in the def chain!");
#endif
  return make_range(def_chain_iterator<T>(MA), def_chain_iterator<T>(UpTo));
}

/// Return a range walking the optimized clobbering-access chain from \p MA.
/// @param MA Access at which to begin the optimized def chain.
/// @return A range walking the optimized clobbering-access chain from \p MA.
template <class T>
inline iterator_range<def_chain_iterator<T, true>> optimized_def_chain(T MA) {
  return make_range(def_chain_iterator<T, true>(MA),
                    def_chain_iterator<T, true>(nullptr));
}

} // end namespace llvm

#endif // LLVM_ANALYSIS_MEMORYSSA_H
