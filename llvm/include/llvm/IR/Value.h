//===- llvm/Value.h - Definition of the Value class -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Value class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_VALUE_H
#define LLVM_IR_VALUE_H

#include "llvm-c/Types.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <iterator>
#include <memory>

namespace llvm {

class APInt;
class Argument;
class BasicBlock;
class Constant;
/// Leaf constant with no operands (for example integers and floating-point literals).
class ConstantData;
/// Constant composed of other constants (for example arrays, structs, and vectors).
class ConstantAggregate;
class DataLayout;
class Function;
class GlobalAlias;
class GlobalIFunc;
class GlobalObject;
class GlobalValue;
class GlobalVariable;
class InlineAsm;
class Instruction;
class LLVMContext;
class MDNode;
class Module;
class ModuleSlotTracker;
class raw_ostream;
template<typename ValueTy> class StringMapEntry;
class Twine;
class User;

/// Entry in a value symbol table mapping a name to a \c Value.
using ValueName = StringMapEntry<Value *>;

//===----------------------------------------------------------------------===//
//                                 Value Class
//===----------------------------------------------------------------------===//

/// LLVM Value Representation
///
/// This is a very important LLVM class. It is the base class of all values
/// computed by a program that may be used as operands to other values. Value is
/// the super class of other important classes such as Instruction and Function.
/// All Values have a Type. Type is not a subclass of Value. Some values can
/// have a name and they belong to some Module.  Setting the name on the Value
/// automatically updates the module's symbol table.
///
/// Every value has a "use list" that keeps track of which other Values are
/// using this Value.  A Value can also have an arbitrary number of ValueHandle
/// objects that watch it and listen to RAUW and Destroy events.  See
/// llvm/IR/ValueHandle.h for details.
class Value {
  const unsigned char SubclassID;   // Subclass identifier (for isa/dyn_cast)
  unsigned char HasValueHandle : 1; // Has a ValueHandle pointing to this?

protected:
  /// Hold arbitary subclass data.
  ///
  /// This member is similar to SubclassData, however it is often used for
  /// holding information which may be used to aid optimization, but which may
  /// be cleared to zero without affecting conservative interpretation.
  unsigned char SubclassOptionalData : 7;

private:
  /// Hold arbitrary subclass data.
  ///
  /// This member is defined by this class, but is not used for anything.
  /// Subclasses can use it to hold whatever state they find useful.  This
  /// field is initialized to zero by the ctor.
  unsigned short SubclassData;

protected:
  /// The number of operands in the subclass.
  ///
  /// This member is defined by this class, but not used for anything.
  /// Subclasses can use it to store their number of operands, if they have
  /// any.
  ///
  /// This is stored here to save space in User on 64-bit hosts.  Since most
  /// instances of Value have operands, 32-bit hosts aren't significantly
  /// affected.
  ///
  /// Note, this should *NOT* be used directly by any class other than User.
  /// User uses this value to find the Use list.
  enum : unsigned {
    NumUserOperandsBits = 28 ///< Width of the NumUserOperands bitfield.
  };
  unsigned NumUserOperands : NumUserOperandsBits; ///< Number of operands in this user.

  // Use the same type as the bitfield above so that MSVC will pack them.
  unsigned IsUsedByMD : 1; ///< True if metadata references this value.
  unsigned HasName : 1; ///< True if this value has a name in the symbol table.
  /// Whether operands are stored in a separately allocated array.
  unsigned HasHungOffUses : 1;
  unsigned HasDescriptor : 1; ///< True if this value has a descriptor.

private:
  Type *VTy;
  Use *UseList = nullptr;

  friend class ValueAsMetadata; // Allow access to IsUsedByMD.
  friend class ValueHandleBase; // Allow access to HasValueHandle.

  template <typename UseT> // UseT == 'Use' or 'const Use'
  class use_iterator_impl {
    friend class Value;

    UseT *U;

    explicit use_iterator_impl(UseT *u) : U(u) {}

  public:
    /// Iterator category for use-list traversal.
    using iterator_category = std::forward_iterator_tag;
    using value_type = UseT;
    using difference_type = std::ptrdiff_t;
    /// Pointer type for a \c Use in the use list.
    using pointer = value_type *;
    /// Reference type for a \c Use in the use list.
    using reference = value_type &;

    /// Default-construct an end iterator.
    use_iterator_impl() : U() {}

    /// Return true if both iterators refer to the same use in the list.
    bool operator==(const use_iterator_impl &x) const { return U == x.U; }
    bool operator!=(const use_iterator_impl &x) const { return !operator==(x); }

    /// Advance to the next use in the list.
    use_iterator_impl &operator++() { // Preincrement
      assert(U && "Cannot increment end iterator!");
      U = U->getNext();
      return *this;
    }

    /// Postincrement to the next use in the list.
    use_iterator_impl operator++(int) { // Postincrement
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    /// Dereference to the current \c Use in the use list.
    UseT &operator*() const {
      assert(U && "Cannot dereference end iterator!");
      return *U;
    }

    /// Access the pointed-to \c Use.
    UseT *operator->() const { return &operator*(); }

    operator use_iterator_impl<const UseT>() const {
      return use_iterator_impl<const UseT>(U);
    }
  };

  template <typename UserTy> // UserTy == 'User' or 'const User'
  class user_iterator_impl {
    use_iterator_impl<Use> UI;
    explicit user_iterator_impl(Use *U) : UI(U) {}
    friend class Value;

  public:
    /// Iterator category for user-list traversal.
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    /// Pointer to a user of this value.
    using value_type = UserTy *;
    /// Pointer type for a user in the user list.
    using pointer = value_type *;
    /// Reference type for a \c User pointer in the user list.
    using reference = value_type &;

    user_iterator_impl() = default;

    /// Compare two user iterators for equality of position in the use-list.
    bool operator==(const user_iterator_impl &x) const { return UI == x.UI; }
    bool operator!=(const user_iterator_impl &x) const { return !operator==(x); }

    /// Returns true if this iterator is equal to user_end() on the value.
    bool atEnd() const { return *this == user_iterator_impl(); }

    /// Advance to the next user (preincrement).
    user_iterator_impl &operator++() { // Preincrement
      ++UI;
      return *this;
    }

    user_iterator_impl operator++(int) { // Postincrement
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    // Retrieve a pointer to the current User.
    UserTy *operator*() const {
      return UI->getUser();
    }

    UserTy *operator->() const { return operator*(); }

    operator user_iterator_impl<const UserTy>() const {
      return user_iterator_impl<const UserTy>(*UI);
    }

    /// Return the underlying \c Use for the current user iterator position.
    Use &getUse() const { return *UI; }
  };

protected:
  /// Construct a value of type \p Ty with subclass identifier \p scid.
  /// \param Ty Type of this value.
  /// \param scid Subclass identifier stored in SubclassID.
  LLVM_ABI Value(Type *Ty, unsigned scid);

  /// Destroy this value; callers must use deleteValue for generic Values.
  ///
  /// Value's destructor should be virtual by design, but that would require
  /// that Value and all of its subclasses have a vtable that effectively
  /// duplicates the information in the value ID. As a size optimization, the
  /// destructor has been protected, and the caller should manually call
  /// deleteValue.
  LLVM_ABI ~Value(); // Use deleteValue() to delete a generic Value.

public:
  /// Construct a deleted copy of \p V; Value is non-copyable.
  /// \param V Unused source value (deleted).
  Value(const Value &V) = delete;
  /// Values are identity objects and cannot be copy-assigned.
  /// \param V Unused source value (deleted).
  Value &operator=(const Value &V) = delete;

  /// Delete a pointer to a generic Value.
  LLVM_ABI void deleteValue();

  /// Support for debugging, callable in GDB: V->dump()
  LLVM_ABI void dump() const;

  /// Print this value to \p O.
  /// \param O Output stream.
  /// \param IsForDebug Whether to use debug-oriented printing.
  LLVM_ABI void print(raw_ostream &O, bool IsForDebug = false) const;
  /// Print this value to \p O using module slot tracker \p MST.
  /// \param O Output stream.
  /// \param MST Module slot tracker for pretty-printing.
  /// \param IsForDebug Whether to use debug-oriented printing.
  LLVM_ABI void print(raw_ostream &O, ModuleSlotTracker &MST,
                      bool IsForDebug = false) const;

  /// Print the name of this Value out to the specified raw_ostream.
  ///
  /// This is useful when you just want to print 'int %reg126', not the
  /// instruction that generated it. If you specify a Module for context, then
  /// even constants get pretty-printed; for example, the type of a null
  /// pointer is printed symbolically.
  /// \param O Output stream.
  /// \param PrintType Whether to include the type in the operand print.
  /// \param M Optional module providing context for pretty-printing constants.
  LLVM_ABI void printAsOperand(raw_ostream &O, bool PrintType = true,
                               const Module *M = nullptr) const;
  /// Print this value as an operand using a module slot tracker.
  /// \param O Output stream.
  /// \param PrintType Whether to include the type in the operand print.
  /// \param MST Module slot tracker for pretty-printing.
  LLVM_ABI void printAsOperand(raw_ostream &O, bool PrintType,
                               ModuleSlotTracker &MST) const;

  /// All values are typed, get the type of this value.
  /// \return The type of this value.
  Type *getType() const { return VTy; }

  /// All values hold a context through their type.
  /// \return The LLVMContext associated with this value's type.
  LLVMContext &getContext() const { return VTy->getContext(); }

  // All values can potentially be named.
  /// Return true if this value has a name in the symbol table.
  /// \return True if this value has a name.
  bool hasName() const { return HasName; }
  /// Return the symbol-table name entry for this value, if any.
  /// \return The ValueName entry, or null if unnamed.
  LLVM_ABI ValueName *getValueName() const;
  /// Set the symbol-table name entry for this value.
  /// \param VN Name entry to associate with this value.
  LLVM_ABI void setValueName(ValueName *VN);

private:
  void destroyValueName();
  enum class ReplaceMetadataUses { No, Yes };
  void doRAUW(Value *New, ReplaceMetadataUses);
  void setNameImpl(const Twine &Name);

public:
  /// Return a constant reference to the value's name.
  ///
  /// This guaranteed to return the same reference as long as the value is not
  /// modified.  If the value has a name, this does a hashtable lookup, so it's
  /// not free.
  /// \return The value's name, or an empty StringRef if unnamed.
  LLVM_ABI StringRef getName() const;

  /// Change the name of the value.
  ///
  /// Choose a new unique name if the provided name is taken.
  ///
  /// \param Name The new name; or "" if the value's name should be removed.
  LLVM_ABI void setName(const Twine &Name);

  /// Transfer the name from V to this value.
  ///
  /// After taking V's name, sets V's name to empty.
  ///
  /// \note It is an error to call V->takeName(V).
  /// \param V Value whose name is transferred to this value.
  LLVM_ABI void takeName(Value *V);

  /// Return this value's name, or a printed operand string if it is unnamed.
  /// \return The name, or an as-operand string when there is no name.
  LLVM_ABI std::string getNameOrAsOperand() const;

  /// Change all uses of this to point to a new Value.
  ///
  /// Go through the uses list for this definition and make each use point to
  /// "V" instead of "this".  After this completes, 'this's use list is
  /// guaranteed to be empty.
  /// \param V Replacement value for every use of this value.
  LLVM_ABI void replaceAllUsesWith(Value *V);

  /// Change non-metadata uses of this to point to a new Value.
  ///
  /// Go through the uses list for this definition and make each use point to
  /// "V" instead of "this". This function skips metadata entries in the list.
  /// \param V Replacement value for non-metadata uses of this value.
  LLVM_ABI void replaceNonMetadataUsesWith(Value *V);

  /// Replace selected uses of this value with \p New.
  ///
  /// Go through the uses list for this definition and make each use point
  /// to "V" if the callback ShouldReplace returns true for the given Use.
  /// Unlike replaceAllUsesWith() this function does not support basic block
  /// values.
  /// \param New Replacement value for selected uses.
  /// \param ShouldReplace Predicate that returns true for uses to replace.
  /// \return True if any uses were replaced.
  LLVM_ABI bool
  replaceUsesWithIf(Value *New, llvm::function_ref<bool(Use &U)> ShouldReplace);

  /// Replace uses of this value that occur outside \p BB with \p V.
  ///
  /// Go through the uses list for this definition and make each use point to
  /// "V" instead of "this" when the use is outside the block. 'This's use list
  /// is expected to have at least one element. Unlike replaceAllUsesWith() this
  /// function does not support basic block values.
  /// \param V Replacement value for out-of-block uses.
  /// \param BB Basic block whose uses of this value are left unchanged.
  LLVM_ABI void replaceUsesOutsideBlock(Value *V, BasicBlock *BB);

  //----------------------------------------------------------------------
  // Methods for handling the chain of uses of this Value.
  //
  // Materializing a function can introduce new uses, so these methods come in
  // two variants:
  // The methods that start with materialized_ check the uses that are
  // currently known given which functions are materialized. Be very careful
  // when using them since you might not get all uses.
  // The methods that don't start with materialized_ assert that modules is
  // fully materialized.
  /// Assert that every module containing this value is fully materialized.
  LLVM_ABI void assertModuleIsMaterializedImpl() const;
  // This indirection exists so we can keep assertModuleIsMaterializedImpl()
  // around in release builds of Value.cpp to be linked with other code built
  // in debug mode. But this avoids calling it in any of the release built code.
  /// Assert in debug builds that modules containing this value are materialized.
  void assertModuleIsMaterialized() const {
#ifndef NDEBUG
    assertModuleIsMaterializedImpl();
#endif
  }

  /// Check if this Value has a use-list.
  /// \return True if this value maintains a use list.
  bool hasUseList() const { return !isa<ConstantData>(this); }

  /// Return true if this value has no uses.
  /// \return True if the use list is empty.
  bool use_empty() const {
    assertModuleIsMaterialized();
    return UseList == nullptr;
  }

  /// Return true if there are no currently materialized uses.
  /// \return True if the materialized use list is empty.
  bool materialized_use_empty() const { return UseList == nullptr; }

  /// Iterator over this value's uses.
  using use_iterator = use_iterator_impl<Use>;
  /// Const iterator over this value's uses.
  using const_use_iterator = use_iterator_impl<const Use>;

  /// Return a mutable iterator to the first materialized use.
  /// \return Iterator to the first known use.
  use_iterator materialized_use_begin() {
    assert(hasUseList());
    return use_iterator(UseList);
  }
  /// Return an iterator to the first materialized use.
  /// \return Const iterator to the first known use.
  const_use_iterator materialized_use_begin() const {
    assert(hasUseList());
    return const_use_iterator(UseList);
  }
  /// Return a mutable iterator to the first use of this value.
  /// \return Iterator to the first use.
  use_iterator use_begin() {
    assertModuleIsMaterialized();
    return materialized_use_begin();
  }
  /// Return an iterator to the first use of this value.
  /// \return Const iterator to the first use.
  const_use_iterator use_begin() const {
    assertModuleIsMaterialized();
    return materialized_use_begin();
  }
  /// Return a past-the-end iterator for the use list.
  /// \return Past-the-end mutable use iterator.
  use_iterator use_end() { return use_iterator(); }
  /// Return a past-the-end const iterator for the use list.
  /// \return Past-the-end const use iterator.
  const_use_iterator use_end() const { return const_use_iterator(); }
  /// Return a range over currently materialized uses.
  /// \return Mutable range of known uses.
  iterator_range<use_iterator> materialized_uses() {
    return make_range(materialized_use_begin(), use_end());
  }
  /// Return a range over uses without requiring module materialization.
  /// \return Const range of known uses.
  iterator_range<const_use_iterator> materialized_uses() const {
    return make_range(materialized_use_begin(), use_end());
  }
  /// Return a mutable range over all uses of this value.
  /// \return Mutable range of uses.
  iterator_range<use_iterator> uses() {
    assertModuleIsMaterialized();
    return materialized_uses();
  }
  /// Return a range over the uses of this value.
  /// \return Const range of uses.
  iterator_range<const_use_iterator> uses() const {
    assertModuleIsMaterialized();
    return materialized_uses();
  }

  /// Return true if this value has no users.
  /// \return True if there are no users.
  bool user_empty() const { return use_empty(); }

  /// Iterator over this value's users.
  using user_iterator = user_iterator_impl<User>;
  /// Const iterator over this value's users.
  using const_user_iterator = user_iterator_impl<const User>;

  /// Return a mutable iterator to the first materialized user.
  /// \return Iterator to the first known user.
  user_iterator materialized_user_begin() {
    assert(hasUseList());
    return user_iterator(UseList);
  }
  /// Return an iterator to the first materialized user.
  /// \return Const iterator to the first known user.
  const_user_iterator materialized_user_begin() const {
    assert(hasUseList());
    return const_user_iterator(UseList);
  }
  /// Return an iterator to the first user of this value.
  /// \return Iterator to the first user.
  user_iterator user_begin() {
    assertModuleIsMaterialized();
    return materialized_user_begin();
  }
  /// Return an iterator to the first user of this value.
  /// \return Const iterator to the first user.
  const_user_iterator user_begin() const {
    assertModuleIsMaterialized();
    return materialized_user_begin();
  }
  /// Return a past-the-end iterator over the users of this value.
  /// \return Past-the-end mutable user iterator.
  user_iterator user_end() { return user_iterator(); }
  /// Return a past-the-end const iterator over the users of this value.
  /// \return Past-the-end const user iterator.
  const_user_iterator user_end() const { return const_user_iterator(); }
  /// Return the sole user when this value has exactly one user.
  /// \return The single user of this value.
  User *user_back() {
    assertModuleIsMaterialized();
    return *materialized_user_begin();
  }
  /// Return the sole user when this value has exactly one user.
  /// \return The single user of this value.
  const User *user_back() const {
    assertModuleIsMaterialized();
    return *materialized_user_begin();
  }
  /// Return a range over currently materialized users.
  /// \return Mutable range of known users.
  iterator_range<user_iterator> materialized_users() {
    return make_range(materialized_user_begin(), user_end());
  }
  /// Return a const range over currently materialized users.
  /// \return Const range of known users.
  iterator_range<const_user_iterator> materialized_users() const {
    return make_range(materialized_user_begin(), user_end());
  }
  /// Return a mutable range over all users of this value.
  /// \return Mutable range of users.
  iterator_range<user_iterator> users() {
    assertModuleIsMaterialized();
    return materialized_users();
  }
  /// Return a const range over all users of this value.
  /// \return Const range of users.
  iterator_range<const_user_iterator> users() const {
    assertModuleIsMaterialized();
    return materialized_users();
  }

  /// Return true if there is exactly one use of this value.
  ///
  /// This is specialized because it is a common request and does not require
  /// traversing the whole use list.
  /// \return True if there is exactly one use.
  bool hasOneUse() const { return UseList && hasSingleElement(uses()); }

  /// Return true if this Value has exactly N uses.
  /// \param N Exact number of uses to test for.
  /// \return True if this value has exactly \p N uses.
  LLVM_ABI bool hasNUses(unsigned N) const;

  /// Return true if this value has N uses or more.
  ///
  /// This is logically equivalent to getNumUses() >= N.
  /// \param N Minimum number of uses to test for.
  /// \return True if this value has at least \p N uses.
  LLVM_ABI bool hasNUsesOrMore(unsigned N) const;

  /// Return true if there is exactly one user of this value.
  ///
  /// Note that this is not the same as "has one use". If a value has one use,
  /// then there certainly is a single user. But if value has several uses,
  /// it is possible that all uses are in a single user, or not.
  ///
  /// This check is potentially costly, since it requires traversing,
  /// in the worst case, the whole use list of a value.
  /// \return True if there is exactly one user.
  LLVM_ABI bool hasOneUser() const;

  /// Return the single undroppable use of this value, if any.
  ///
  /// Returns the use when there is exactly one use of this value that cannot be
  /// dropped.
  /// \return The single undroppable use, or null if there is not exactly one.
  LLVM_ABI Use *getSingleUndroppableUse();
  /// Return the single undroppable use of this value, if any.
  /// \return The single undroppable use, or null if there is not exactly one.
  const Use *getSingleUndroppableUse() const {
    return const_cast<Value *>(this)->getSingleUndroppableUse();
  }

  /// Return the unique undroppable user of this value, if any.
  ///
  /// Returns the user when there is exactly one unique user of this value that
  /// cannot be dropped (that user can have multiple uses of this value).
  /// \return The unique undroppable user, or null if there is not exactly one.
  LLVM_ABI User *getUniqueUndroppableUser();
  /// Return the unique undroppable user of this value, if any.
  /// \return The unique undroppable user, or null if there is not exactly one.
  const User *getUniqueUndroppableUser() const {
    return const_cast<Value *>(this)->getUniqueUndroppableUser();
  }

  /// Return true if this value has exactly N undroppable uses.
  ///
  /// This is specialized because it is a common request and does not require
  /// traversing the whole use list.
  /// \param N Exact number of undroppable uses to test for.
  /// \return True if there are exactly \p N undroppable uses.
  LLVM_ABI bool hasNUndroppableUses(unsigned N) const;

  /// Return true if this value has N undroppable uses or more.
  ///
  /// This is logically equivalent to getNumUses() >= N.
  /// \param N Minimum number of undroppable uses to test for.
  /// \return True if there are at least \p N undroppable uses.
  LLVM_ABI bool hasNUndroppableUsesOrMore(unsigned N) const;

  /// Remove every uses that can safely be removed.
  ///
  /// This will remove for example uses in llvm.assume.
  /// This should be used when performing want to perform a transformation but
  /// some Droppable uses prevent it.
  /// This function optionally takes a filter to only remove some droppable
  /// uses.
  /// \param ShouldDrop Predicate selecting which droppable uses to remove.
  LLVM_ABI void
  dropDroppableUses(llvm::function_ref<bool(const Use *)> ShouldDrop =
                        [](const Use *) { return true; });

  /// Remove every use of this value in \p User that can safely be removed.
  /// \param Usr User whose droppable uses of this value are removed.
  LLVM_ABI void dropDroppableUsesIn(User &Usr);

  /// Remove the droppable use \p U.
  /// \param U Droppable use to remove.
  LLVM_ABI static void dropDroppableUse(Use &U);

  /// Check if this value is used in the specified basic block.
  ///
  /// Not supported for ConstantData.
  /// \param BB Basic block to search for uses.
  /// \return True if this value is used in \p BB.
  LLVM_ABI bool isUsedInBasicBlock(const BasicBlock *BB) const;

  /// This method computes the number of uses of this Value.
  ///
  /// This is a linear time operation.  Use hasOneUse, hasNUses, or
  /// hasNUsesOrMore to check for specific values.
  /// \return The number of uses of this value.
  LLVM_ABI unsigned getNumUses() const;

  /// This method should only be used by the Use class.
  /// \param U Use to prepend onto this value's use list.
  void addUse(Use &U) {
    if (hasUseList())
      U.addToList(&UseList);
  }

  /// Concrete subclass of this.
  ///
  /// An enumeration for keeping track of the concrete subclass of Value that
  /// is actually instantiated. Values of this enumeration are kept in the
  /// Value classes SubclassID field. They are used for concrete type
  /// identification.
  enum ValueTy {
#define HANDLE_VALUE(Name) Name##Val,
#include "llvm/IR/Value.def"

    // Markers:
#define HANDLE_CONSTANT_MARKER(Marker, Constant) Marker = Constant##Val,
#include "llvm/IR/Value.def"
  };

  /// Return an ID for the concrete type of this object.
  ///
  /// This is used to implement the classof checks.  This should not be used
  /// for any other purpose, as the values may change as LLVM evolves.  Also,
  /// note that for instructions, the Instruction's opcode is added to
  /// InstructionVal. So this means three things:
  /// # there is no value with code InstructionVal (no opcode==0).
  /// # there are more possible values for the value type than in ValueTy enum.
  /// # the InstructionVal enumerator must be the highest valued enumerator in
  ///   the ValueTy enum.
  /// \return The concrete subclass identifier for this value.
  unsigned getValueID() const {
    return SubclassID;
  }

  /// Return the raw optional flags value contained in this value.
  ///
  /// This should only be used when testing two Values for equivalence.
  /// \return The raw SubclassOptionalData bits.
  unsigned getRawSubclassOptionalData() const {
    return SubclassOptionalData;
  }

  /// Return true if there is a value handle associated with this value.
  /// \return True if a ValueHandle watches this value.
  bool hasValueHandle() const { return HasValueHandle; }

  /// Return true if there is metadata referencing this value.
  /// \return True if metadata references this value.
  bool isUsedByMetadata() const { return IsUsedByMD; }

protected:
  /// Get the current metadata attachments for the given kind, if any.
  ///
  /// These functions require that the value have at most a single attachment
  /// of the given kind, and return \c nullptr if such an attachment is missing.
  /// \param Kind Metadata kind name to look up.
  /// \return The attached MDNode, or null if none is present.
  LLVM_ABI MDNode *getMetadata(StringRef Kind) const LLVM_READONLY;

private:
  LLVM_ABI unsigned getMetadataIndex() const;
  LLVM_ABI unsigned &getMetadataIndex();

protected:
  /// Append all metadata attached to this value into \p MDs.
  ///
  /// Attachments are sorted by KindID. The first element of each pair returned
  /// is the KindID, the second element is the metadata value. Attachments with
  /// the same ID appear in insertion order.
  /// \param MDs Destination vector that receives kind/metadata pairs.
  LLVM_ABI void
  getAllMetadata(SmallVectorImpl<std::pair<unsigned, MDNode *>> &MDs) const;

  /// Set a particular kind of metadata attachment.
  ///
  /// Sets the given attachment to \p Node, erasing it if \p Node is \c nullptr
  /// or replacing it if it already exists.
  /// \param KindID Metadata kind identifier.
  /// \param Node Metadata node to attach, or null to erase.
  LLVM_ABI void setMetadata(unsigned KindID, MDNode *Node);
  /// Set a particular kind of metadata attachment by kind name.
  ///
  /// Sets the given attachment to \p Node, erasing it if \p Node is \c nullptr
  /// or replacing it if it already exists.
  /// \param Kind Metadata kind name.
  /// \param Node Metadata node to attach, or null to erase.
  LLVM_ABI void setMetadata(StringRef Kind, MDNode *Node);

  /// Add a metadata attachment.
  /// \param KindID Metadata kind identifier.
  /// \param MD Metadata node to attach.
  LLVM_ABI void addMetadata(unsigned KindID, MDNode &MD);
  /// Add a metadata attachment identified by kind name \p Kind.
  /// \param Kind Metadata kind name.
  /// \param MD Metadata node to attach.
  LLVM_ABI void addMetadata(StringRef Kind, MDNode &MD);

  /// Erase all metadata attachments with the given kind.
  ///
  /// \param KindID Metadata kind identifier to erase.
  /// \return True if any metadata was removed.
  LLVM_ABI bool eraseMetadata(unsigned KindID);

  /// Erase all metadata attachments matching the given predicate.
  /// \param Pred Predicate selecting (KindID, MDNode) attachments to erase.
  LLVM_ABI void eraseMetadataIf(function_ref<bool(unsigned, MDNode *)> Pred);

  /// Erase all metadata attached to this Value.
  LLVM_ABI void clearMetadata();

  /// Get metadata for the given kind, if any.
  ///
  /// This is an internal function that must only be called after
  /// checking that `hasMetadata()` returns true.
  /// \param KindID Metadata kind identifier.
  /// \return The attached MDNode for \p KindID.
  LLVM_ABI MDNode *getMetadataImpl(unsigned KindID) const LLVM_READONLY;

public:
  /// Return true if this value is a swifterror value.
  ///
  /// swifterror values can be either a function argument or an alloca with a
  /// swifterror attribute.
  /// \return True if this value is a swifterror value.
  LLVM_ABI bool isSwiftError() const;

  /// Strip off pointer casts, all-zero GEPs and address space casts.
  ///
  /// Returns the original uncasted value.  If this is called on a non-pointer
  /// value, it returns 'this'.
  /// \return The underlying uncasted value, or this if not a pointer.
  LLVM_ABI const Value *stripPointerCasts() const;
  /// Non-const overload of \c stripPointerCasts().
  /// \return The underlying uncasted value, or this if not a pointer.
  Value *stripPointerCasts() {
    return const_cast<Value *>(
        static_cast<const Value *>(this)->stripPointerCasts());
  }

  /// Strip off pointer casts, all-zero GEPs, address space casts, and aliases.
  ///
  /// Returns the original uncasted value.  If this is called on a non-pointer
  /// value, it returns 'this'.
  /// \return The underlying uncasted value, or this if not a pointer.
  LLVM_ABI const Value *stripPointerCastsAndAliases() const;
  /// Non-const overload of \c stripPointerCastsAndAliases().
  /// \return The underlying uncasted value, or this if not a pointer.
  Value *stripPointerCastsAndAliases() {
    return const_cast<Value *>(
        static_cast<const Value *>(this)->stripPointerCastsAndAliases());
  }

  /// Strip off pointer casts, all-zero GEPs and address space casts
  /// but ensures the representation of the result stays the same.
  ///
  /// Returns the original uncasted value with the same representation. If this
  /// is called on a non-pointer value, it returns 'this'.
  /// \return The uncasted value with the same representation, or this.
  LLVM_ABI const Value *stripPointerCastsSameRepresentation() const;
  /// Non-const overload of \c stripPointerCastsSameRepresentation().
  /// \return The uncasted value with the same representation, or this.
  Value *stripPointerCastsSameRepresentation() {
    return const_cast<Value *>(static_cast<const Value *>(this)
                                   ->stripPointerCastsSameRepresentation());
  }

  /// Strip off pointer casts, all-zero GEPs, single-argument phi nodes and
  /// invariant group info.
  ///
  /// Returns the original uncasted value.  If this is called on a non-pointer
  /// value, it returns 'this'. This function should be used only in
  /// Alias analysis.
  /// \return The underlying uncasted value for alias analysis, or this.
  LLVM_ABI const Value *stripPointerCastsForAliasAnalysis() const;
  /// Non-const overload of \c stripPointerCastsForAliasAnalysis().
  /// \return The underlying uncasted value for alias analysis, or this.
  Value *stripPointerCastsForAliasAnalysis() {
    return const_cast<Value *>(static_cast<const Value *>(this)
                                   ->stripPointerCastsForAliasAnalysis());
  }

  /// Strip off pointer casts and all-constant inbounds GEPs.
  ///
  /// Returns the original pointer value.  If this is called on a non-pointer
  /// value, it returns 'this'.
  /// \return The underlying pointer value, or this if not a pointer.
  LLVM_ABI const Value *stripInBoundsConstantOffsets() const;
  /// Non-const overload of \c stripInBoundsConstantOffsets().
  /// \return The underlying pointer value, or this if not a pointer.
  Value *stripInBoundsConstantOffsets() {
    return const_cast<Value *>(
              static_cast<const Value *>(this)->stripInBoundsConstantOffsets());
  }

  /// Accumulate constant GEP offsets from this pointer and return its base.
  ///
  /// Only 'getelementptr' instructions (GEPs) are accumulated but other
  /// instructions, e.g., casts, are stripped away as well.
  /// The accumulated constant offset is added to \p Offset and the base
  /// pointer is returned.
  ///
  /// The APInt \p Offset has to have a bit-width equal to the IntPtr type for
  /// the address space of 'this' pointer value, e.g., use
  /// DataLayout::getIndexTypeSizeInBits(Ty).
  ///
  /// If \p AllowNonInbounds is true, offsets in GEPs are stripped and
  /// accumulated even if the GEP is not "inbounds".
  ///
  /// If \p AllowInvariantGroup is true then this method also looks through
  /// strip.invariant.group and launder.invariant.group intrinsics.
  ///
  /// If \p ExternalAnalysis is provided it will be used to calculate a offset
  /// when a operand of GEP is not constant.
  /// For example, for a value \p ExternalAnalysis might try to calculate a
  /// lower bound. If \p ExternalAnalysis is successful, it should return true.
  ///
  /// If \p LookThroughIntToPtr is true then this method also looks through
  /// IntToPtr and PtrToInt constant expressions. The returned pointer may not
  /// have the same provenance as this value.
  ///
  /// If this is called on a non-pointer value, it returns 'this' and the
  /// \p Offset is not modified.
  ///
  /// Note that this function will never return a nullptr. It will also never
  /// manipulate the \p Offset in a way that would not match the difference
  /// between the underlying value and the returned one. Thus, if a variable
  /// offset is encountered during traversal, the returned value is the first
  /// traversed Value that introduces a non-constant offset and \p Offset is the
  /// accumulated constant offset up to that point.
  /// \param DL Data layout used to size indices and pointers.
  /// \param Offset APInt that accumulates the constant byte offset.
  /// \param AllowNonInbounds Whether to accumulate non-inbounds GEP offsets.
  /// \param AllowInvariantGroup Whether to look through invariant.group intrinsics.
  /// \param ExternalAnalysis Optional analysis for non-constant GEP operands.
  /// \param LookThroughIntToPtr Whether to look through IntToPtr/PtrToInt.
  /// \return The base pointer value after stripping accumulated offsets.
  LLVM_ABI const Value *stripAndAccumulateConstantOffsets(
      const DataLayout &DL, APInt &Offset, bool AllowNonInbounds,
      bool AllowInvariantGroup = false,
      function_ref<bool(Value &Value, APInt &Offset)> ExternalAnalysis =
          nullptr,
      bool LookThroughIntToPtr = false) const;

  /// Non-const overload of stripAndAccumulateConstantOffsets.
  /// \param DL Data layout used to size indices and pointers.
  /// \param Offset APInt that accumulates the constant byte offset.
  /// \param AllowNonInbounds Whether to accumulate non-inbounds GEP offsets.
  /// \param AllowInvariantGroup Whether to look through invariant.group intrinsics.
  /// \param ExternalAnalysis Optional analysis for non-constant GEP operands.
  /// \param LookThroughIntToPtr Whether to look through IntToPtr/PtrToInt.
  /// \return The base pointer value after stripping accumulated offsets.
  Value *stripAndAccumulateConstantOffsets(
      const DataLayout &DL, APInt &Offset, bool AllowNonInbounds,
      bool AllowInvariantGroup = false,
      function_ref<bool(Value &Value, APInt &Offset)> ExternalAnalysis =
          nullptr,
      bool LookThroughIntToPtr = false) {
    return const_cast<Value *>(
        static_cast<const Value *>(this)->stripAndAccumulateConstantOffsets(
            DL, Offset, AllowNonInbounds, AllowInvariantGroup, ExternalAnalysis,
            LookThroughIntToPtr));
  }

  /// This is a wrapper around stripAndAccumulateConstantOffsets with the
  /// in-bounds requirement set to false.
  /// \param DL Data layout used to size indices and pointers.
  /// \param Offset APInt that accumulates the constant byte offset.
  /// \return The base pointer value after stripping in-bounds offsets.
  const Value *stripAndAccumulateInBoundsConstantOffsets(const DataLayout &DL,
                                                         APInt &Offset) const {
    return stripAndAccumulateConstantOffsets(DL, Offset,
                                             /* AllowNonInbounds */ false);
  }
  /// Non-const overload of \c stripAndAccumulateInBoundsConstantOffsets().
  /// \param DL Data layout used to size indices and pointers.
  /// \param Offset APInt that accumulates the constant byte offset.
  /// \return The base pointer value after stripping in-bounds offsets.
  Value *stripAndAccumulateInBoundsConstantOffsets(const DataLayout &DL,
                                                   APInt &Offset) {
    return stripAndAccumulateConstantOffsets(DL, Offset,
                                             /* AllowNonInbounds */ false);
  }

  /// Strip off pointer casts and inbounds GEPs.
  ///
  /// Returns the original pointer value.  If this is called on a non-pointer
  /// value, it returns 'this'.
  /// \param Func Optional callback invoked for each stripped value.
  /// \return The underlying pointer value, or this if not a pointer.
  LLVM_ABI const Value *stripInBoundsOffsets(
      function_ref<void(const Value *)> Func = [](const Value *) {}) const;
  /// Non-const overload of \c stripInBoundsOffsets().
  /// \param Func Optional callback invoked for each stripped value.
  /// \return The underlying pointer value, or this if not a pointer.
  inline Value *stripInBoundsOffsets(function_ref<void(const Value *)> Func =
                                  [](const Value *) {}) {
    return const_cast<Value *>(
        static_cast<const Value *>(this)->stripInBoundsOffsets(Func));
  }

  /// If this ptr is provably equal to \p Other plus a constant offset, return
  /// that offset in bytes. Essentially `ptr this` subtract `ptr Other`.
  /// \param Other Pointer to compare against.
  /// \param DL Data layout used to compute the offset.
  /// \return The constant byte offset, or nullopt if not provable.
  LLVM_ABI std::optional<int64_t>
  getPointerOffsetFrom(const Value *Other, const DataLayout &DL) const;

  /// Return true if this pointer's memory may be freed in its defining scope.
  ///
  /// Return true if the memory object referred to by V can by freed in the
  /// scope for which the SSA value defining the allocation is statically
  /// defined.  E.g.  deallocation after the static scope of a value does not
  /// count, but a deallocation before that does.
  /// \return True if the pointed-to memory may be freed in this scope.
  LLVM_ABI bool canBeFreed() const;

  /// Returns the number of bytes known to be dereferenceable for the
  /// pointer value.
  ///
  /// If CanBeNull is set by this function the pointer can either be null or be
  /// dereferenceable up to the returned number of bytes.
  ///
  /// If CanBeFreed is non-null, it will be populated with information on
  /// whether the pointer might be freed, i.e. is only known dereferenceable
  /// at the point of definition. By passing null the caller indicates that it
  /// does not care.
  /// \param DL Data layout used for pointer-size queries.
  /// \param CanBeNull Set to true if the pointer may be null.
  /// \param CanBeFreed Optional out-parameter for whether the memory may be freed.
  /// \return Number of known dereferenceable bytes.
  LLVM_ABI uint64_t getPointerDereferenceableBytes(const DataLayout &DL,
                                                   bool &CanBeNull,
                                                   bool *CanBeFreed) const;

  /// Returns an alignment of the pointer value.
  ///
  /// Returns an alignment which is either specified explicitly, e.g. via
  /// align attribute of a function argument, or guaranteed by DataLayout.
  /// \param DL Data layout used when no explicit alignment is present.
  /// \return The known alignment of this pointer value.
  LLVM_ABI Align getPointerAlignment(const DataLayout &DL) const;

  /// Translate PHI node to its predecessor from the given basic block.
  ///
  /// If this value is a PHI node with CurBB as its parent, return the value in
  /// the PHI node corresponding to PredBB.  If not, return ourself.  This is
  /// useful if you want to know the value something has in a predecessor
  /// block.
  /// \param CurBB Basic block that is the current parent of a PHI.
  /// \param PredBB Predecessor basic block whose incoming value is requested.
  /// \return The PHI incoming value from \p PredBB, or this value otherwise.
  LLVM_ABI const Value *DoPHITranslation(const BasicBlock *CurBB,
                                         const BasicBlock *PredBB) const;
  /// Non-const overload of \c DoPHITranslation().
  /// \param CurBB Basic block that is the current parent of a PHI.
  /// \param PredBB Predecessor basic block whose incoming value is requested.
  /// \return The PHI incoming value from \p PredBB, or this value otherwise.
  Value *DoPHITranslation(const BasicBlock *CurBB, const BasicBlock *PredBB) {
    return const_cast<Value *>(
             static_cast<const Value *>(this)->DoPHITranslation(CurBB, PredBB));
  }

  /// The maximum alignment for instructions.
  ///
  /// This is the greatest alignment value supported by load, store, and alloca
  /// instructions, and global values.
  static constexpr unsigned MaxAlignmentExponent = 32;
  /// Maximum absolute alignment in bytes (\c 1 << MaxAlignmentExponent).
  static constexpr uint64_t MaximumAlignment = 1ULL << MaxAlignmentExponent;

  /// Mutate the type of this Value to be of the specified type.
  ///
  /// Note that this is an extremely dangerous operation which can create
  /// completely invalid IR very easily.  It is strongly recommended that you
  /// recreate IR objects with the right types instead of mutating them in
  /// place.
  /// \param Ty New type to assign to this value.
  void mutateType(Type *Ty) {
    VTy = Ty;
  }

  /// Sort the use-list.
  ///
  /// Sorts the Value's use-list by Cmp using a stable mergesort.  Cmp is
  /// expected to compare two \a Use references.
  /// \param Cmp Comparator over two \a Use references.
  template <class Compare> void sortUseList(Compare Cmp);

  /// Reverse the use-list.
  LLVM_ABI void reverseUseList();

private:
  /// Merge two lists together.
  ///
  /// Merges \c L and \c R using \c Cmp.  To enable stable sorts, always pushes
  /// "equal" items from L before items from R.
  ///
  /// \return the first element in the list.
  ///
  /// \note Completely ignores \a Use::Prev (doesn't read, doesn't update).
  template <class Compare>
  static Use *mergeUseLists(Use *L, Use *R, Compare Cmp) {
    Use *Merged;
    Use **Next = &Merged;

    while (true) {
      if (!L) {
        *Next = R;
        break;
      }
      if (!R) {
        *Next = L;
        break;
      }
      if (Cmp(*R, *L)) {
        *Next = R;
        Next = &R->Next;
        R = R->Next;
      } else {
        *Next = L;
        Next = &L->Next;
        L = L->Next;
      }
    }

    return Merged;
  }

protected:
  /// Return the opaque subclass data bits stored in this value.
  /// \return The SubclassData bitfield value.
  unsigned short getSubclassDataFromValue() const { return SubclassData; }
  /// Set the opaque subclass data bits stored in this value.
  /// \param D New subclass data bits to store.
  void setValueSubclassData(unsigned short D) { SubclassData = D; }
};

/// Deleter for \c unique_value that calls \c Value::deleteValue().
struct ValueDeleter {
  /// Delete \p V using \c Value::deleteValue().
  /// \param V Value to destroy.
  void operator()(Value *V) { V->deleteValue(); }
};

/// Unique ownership pointer for \c Value that calls \c deleteValue.
///
/// Use this instead of std::unique_ptr<Value> or std::unique_ptr<Instruction>.
/// Those don't work because Value and Instruction's destructors are protected,
/// aren't virtual, and won't destroy the complete object.
using unique_value = std::unique_ptr<Value, ValueDeleter>;

/// Print \p V to \p OS.
/// \param OS Output stream.
/// \param V Value to print.
/// \return \p OS after printing.
inline raw_ostream &operator<<(raw_ostream &OS, const Value &V) {
  V.print(OS);
  return OS;
}

/// Set this use to refer to \p Val, updating use lists.
/// \param Val Value this use should refer to.
void Use::set(Value *V) {
  removeFromList();
  Val = V;
  if (V)
    V->addUse(*this);
}

/// Assign \p RHS as the value of this use.
/// \param RHS Value to assign.
/// \return \p RHS.
Value *Use::operator=(Value *RHS) {
  set(RHS);
  return RHS;
}

const Use &Use::operator=(const Use &RHS) {
  set(RHS.Val);
  return *this;
}

template <class Compare> void Value::sortUseList(Compare Cmp) {
  if (!UseList || !UseList->Next)
    // No need to sort 0 or 1 uses.
    return;

  // Note: this function completely ignores Prev pointers until the end when
  // they're fixed en masse.

  // Create a binomial vector of sorted lists, visiting uses one at a time and
  // merging lists as necessary.
  const unsigned MaxSlots = 32;
  Use *Slots[MaxSlots];

  // Collect the first use, turning it into a single-item list.
  Use *Next = UseList->Next;
  UseList->Next = nullptr;
  unsigned NumSlots = 1;
  Slots[0] = UseList;

  // Collect all but the last use.
  while (Next->Next) {
    Use *Current = Next;
    Next = Current->Next;

    // Turn Current into a single-item list.
    Current->Next = nullptr;

    // Save Current in the first available slot, merging on collisions.
    unsigned I;
    for (I = 0; I < NumSlots; ++I) {
      if (!Slots[I])
        break;

      // Merge two lists, doubling the size of Current and emptying slot I.
      //
      // Since the uses in Slots[I] originally preceded those in Current, send
      // Slots[I] in as the left parameter to maintain a stable sort.
      Current = mergeUseLists(Slots[I], Current, Cmp);
      Slots[I] = nullptr;
    }
    // Check if this is a new slot.
    if (I == NumSlots) {
      ++NumSlots;
      assert(NumSlots <= MaxSlots && "Use list bigger than 2^32");
    }

    // Found an open slot.
    Slots[I] = Current;
  }

  // Merge all the lists together.
  assert(Next && "Expected one more Use");
  assert(!Next->Next && "Expected only one Use");
  UseList = Next;
  for (unsigned I = 0; I < NumSlots; ++I)
    if (Slots[I])
      // Since the uses in Slots[I] originally preceded those in UseList, send
      // Slots[I] in as the left parameter to maintain a stable sort.
      UseList = mergeUseLists(Slots[I], UseList, Cmp);

  // Fix the Prev pointers.
  for (Use *I = UseList, **Prev = &UseList; I; I = I->Next) {
    I->Prev = Prev;
    Prev = &I->Next;
  }
}

// isa - Provide some specializations of isa so that we don't have to include
// the subtype header files to test to see if the value is a subclass...
//
/// Specialization of \c isa_impl so \c isa<Constant> works without Constants.h.
template <> struct isa_impl<Constant, Value> {
  /// Return true if \p Val is a Constant.
  /// \param Val Value to test.
  /// \return True if \p Val is in the Constant value-ID range.
  static inline bool doit(const Value &Val) {
    static_assert(Value::ConstantFirstVal == 0,
                  "Val.getValueID() >= Value::ConstantFirstVal");
    return Val.getValueID() <= Value::ConstantLastVal;
  }
};

/// Specialization of \c isa_impl so \c isa<ConstantData> works without Constants.h.
template <> struct isa_impl<ConstantData, Value> {
  /// Return true if \p Val is a ConstantData.
  /// \param Val Value to test.
  /// \return True if \p Val is in the ConstantData value-ID range.
  static inline bool doit(const Value &Val) {
    static_assert(Value::ConstantDataFirstVal == 0,
                  "Val.getValueID() >= Value::ConstantDataFirstVal");
    return Val.getValueID() <= Value::ConstantDataLastVal;
  }
};

/// Specialization of \c isa_impl so \c isa<ConstantAggregate> works without Constants.h.
template <> struct isa_impl<ConstantAggregate, Value> {
  /// Return true if \p Val is a ConstantAggregate.
  /// \param Val Value to test.
  /// \return True if \p Val is in the ConstantAggregate value-ID range.
  static inline bool doit(const Value &Val) {
    return Val.getValueID() >= Value::ConstantAggregateFirstVal &&
           Val.getValueID() <= Value::ConstantAggregateLastVal;
  }
};

/// Specialization of \c isa_impl so \c isa<Argument> works without Argument.h.
template <> struct isa_impl<Argument, Value> {
  /// Return true if \p Val is an Argument.
  /// \param Val Value to test.
  /// \return True if \p Val has ArgumentVal.
  static inline bool doit (const Value &Val) {
    return Val.getValueID() == Value::ArgumentVal;
  }
};

/// Specialization of \c isa_impl so \c isa<InlineAsm> works without InlineAsm.h.
template <> struct isa_impl<InlineAsm, Value> {
  /// Return true if \p Val is an InlineAsm.
  /// \param Val Value to test.
  /// \return True if \p Val has InlineAsmVal.
  static inline bool doit(const Value &Val) {
    return Val.getValueID() == Value::InlineAsmVal;
  }
};

/// Specialization of \c isa_impl so \c isa<Instruction> works without Instruction.h.
template <> struct isa_impl<Instruction, Value> {
  /// Return true if \p Val is an Instruction.
  /// \param Val Value to test.
  /// \return True if \p Val's value ID is at least InstructionVal.
  static inline bool doit(const Value &Val) {
    return Val.getValueID() >= Value::InstructionVal;
  }
};

/// Specialization of \c isa_impl so \c isa<BasicBlock> works without BasicBlock.h.
template <> struct isa_impl<BasicBlock, Value> {
  /// Return true if \p Val is a BasicBlock.
  /// \param Val Value to test.
  /// \return True if \p Val has BasicBlockVal.
  static inline bool doit(const Value &Val) {
    return Val.getValueID() == Value::BasicBlockVal;
  }
};

/// Specialization of \c isa_impl so \c isa<Function> works without Function.h.
template <> struct isa_impl<Function, Value> {
  /// Return true if \p Val is a Function.
  /// \param Val Value to test.
  /// \return True if \p Val has FunctionVal.
  static inline bool doit(const Value &Val) {
    return Val.getValueID() == Value::FunctionVal;
  }
};

/// Specialization of \c isa_impl so \c isa<GlobalVariable> works without GlobalVariable.h.
template <> struct isa_impl<GlobalVariable, Value> {
  /// Return true if \p Val is a GlobalVariable.
  /// \param Val Value to test.
  /// \return True if \p Val has GlobalVariableVal.
  static inline bool doit(const Value &Val) {
    return Val.getValueID() == Value::GlobalVariableVal;
  }
};

/// Specialization of \c isa_impl so \c isa<GlobalAlias> works without GlobalAlias.h.
template <> struct isa_impl<GlobalAlias, Value> {
  /// Return true if \p Val is a GlobalAlias.
  /// \param Val Value to test.
  /// \return True if \p Val has GlobalAliasVal.
  static inline bool doit(const Value &Val) {
    return Val.getValueID() == Value::GlobalAliasVal;
  }
};

/// Specialization of \c isa_impl so \c isa<GlobalIFunc> works without GlobalIFunc.h.
template <> struct isa_impl<GlobalIFunc, Value> {
  /// Return true if \p Val is a GlobalIFunc.
  /// \param Val Value to test.
  /// \return True if \p Val has GlobalIFuncVal.
  static inline bool doit(const Value &Val) {
    return Val.getValueID() == Value::GlobalIFuncVal;
  }
};

/// Specialization of \c isa_impl so \c isa<GlobalValue> works without GlobalValue.h.
template <> struct isa_impl<GlobalValue, Value> {
  /// Return true if \p Val is a GlobalValue.
  /// \param Val Value to test.
  /// \return True if \p Val is a GlobalObject or GlobalAlias.
  static inline bool doit(const Value &Val) {
    return isa<GlobalObject>(Val) || isa<GlobalAlias>(Val);
  }
};

/// Specialization of \c isa_impl so \c isa<GlobalObject> works without GlobalObject.h.
template <> struct isa_impl<GlobalObject, Value> {
  /// Return true if \p Val is a GlobalObject.
  /// \param Val Value to test.
  /// \return True if \p Val is a GlobalVariable, Function, or GlobalIFunc.
  static inline bool doit(const Value &Val) {
    return isa<GlobalVariable>(Val) || isa<Function>(Val) ||
           isa<GlobalIFunc>(Val);
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Convert an opaque \c LLVMValueRef to a \c Value pointer.
/// \param P Opaque C API value reference to unwrap.
/// \return The corresponding \c Value pointer.
inline Value *unwrap(LLVMValueRef P) {
  return reinterpret_cast<Value *>(P);
}

/// Convert a \c Value pointer to an opaque \c LLVMValueRef.
/// \param P Value to wrap for the C API.
/// \return The opaque \c LLVMValueRef for \p P.
inline LLVMValueRef wrap(const Value *P) {
  return reinterpret_cast<LLVMValueRef>(const_cast<Value *>(P));
}

/// Unwrap an opaque \c LLVMValueRef as a \c Value subclass.
/// \param P Opaque C API value reference to unwrap.
/// \return \p P cast to subclass \c T.
template <typename T>
inline T *unwrap(LLVMValueRef P) {
  return cast<T>(unwrap(P));
}

/// Unwrap an array of opaque \c LLVMValueRef values as \c Value pointers.
/// \param Vals Array of opaque value references.
/// \return The array reinterpreted as \c Value pointers.
inline Value **unwrap(LLVMValueRef *Vals) {
  return reinterpret_cast<Value**>(Vals);
}

/// Unwrap an opaque \c LLVMValueRef array as pointers to \c Value subclass \p T.
/// \param Vals Array of opaque value references.
/// \param Length Number of elements in \p Vals.
/// \return The array reinterpreted as pointers to \c T.
template<typename T>
inline T **unwrap(LLVMValueRef *Vals, unsigned Length) {
#ifndef NDEBUG
  for (LLVMValueRef *I = Vals, *E = Vals + Length; I != E; ++I)
    unwrap<T>(*I); // For side effect of calling assert on invalid usage.
#endif
  (void)Length;
  return reinterpret_cast<T**>(Vals);
}

/// Wrap an array of \c Value pointers as opaque \c LLVMValueRef values.
/// \param Vals Array of Value pointers.
/// \return The array reinterpreted as opaque \c LLVMValueRef values.
inline LLVMValueRef *wrap(const Value **Vals) {
  return reinterpret_cast<LLVMValueRef*>(const_cast<Value**>(Vals));
}

} // end namespace llvm

#endif // LLVM_IR_VALUE_H
