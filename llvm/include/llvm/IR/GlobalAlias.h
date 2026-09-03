//===-------- llvm/GlobalAlias.h - GlobalAlias class ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the GlobalAlias class, which
// represents a single function or variable alias in the IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GLOBALALIAS_H
#define LLVM_IR_GLOBALALIAS_H

#include "llvm/ADT/ilist_node.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Twine;
class Module;
template <typename ValueSubClass, typename... Args> class SymbolTableListTraits;

/// Represents a single function or variable alias in the IR.
class GlobalAlias : public GlobalValue, public ilist_node<GlobalAlias> {
  friend class SymbolTableListTraits<GlobalAlias>;

  constexpr static IntrusiveOperandsAllocMarker AllocMarker{1};

  /// Construct a global alias with the given type, linkage, name, and aliasee.
  /// \param Ty The value type of the alias.
  /// \param AddressSpace The address space of the alias pointer.
  /// \param Linkage The linkage type for the alias.
  /// \param Name The name of the alias.
  /// \param Aliasee The constant this alias refers to.
  /// \param Parent The parent module, or null to leave the alias unattached.
  GlobalAlias(Type *Ty, unsigned AddressSpace, LinkageTypes Linkage,
              const Twine &Name, Constant *Aliasee, Module *Parent);

public:
  /// Copy construction is deleted; GlobalAlias owns module list state.
  /// \param Other The alias that would be copied (deleted).
  GlobalAlias(const GlobalAlias &Other) = delete;
  /// Assignment is deleted; GlobalAlias owns module list state.
  /// \param Other The alias that would be assigned from (deleted).
  GlobalAlias &operator=(const GlobalAlias &Other) = delete;

  /// Create a global alias and optionally insert it into a module.
  ///
  /// If a parent module is specified, the alias is automatically inserted into
  /// the end of the specified module's alias list.
  /// \param Ty The value type of the alias.
  /// \param AddressSpace The address space of the alias pointer.
  /// \param Linkage The linkage type for the alias.
  /// \param Name The name of the alias.
  /// \param Aliasee The constant this alias refers to.
  /// \param Parent The parent module, or null to leave the alias unattached.
  /// \return The newly created alias.
  LLVM_ABI static GlobalAlias *create(Type *Ty, unsigned AddressSpace,
                                      LinkageTypes Linkage, const Twine &Name,
                                      Constant *Aliasee, Module *Parent);

  /// Create a global alias without an aliasee, optionally inserting into a
  /// module.
  /// \param Ty The value type of the alias.
  /// \param AddressSpace The address space of the alias pointer.
  /// \param Linkage The linkage type for the alias.
  /// \param Name The name of the alias.
  /// \param Parent The parent module, or null to leave the alias unattached.
  /// \return The newly created alias.
  LLVM_ABI static GlobalAlias *create(Type *Ty, unsigned AddressSpace,
                                      LinkageTypes Linkage, const Twine &Name,
                                      Module *Parent);

  /// Create a global alias whose parent module is taken from the aliasee.
  /// \param Ty The value type of the alias.
  /// \param AddressSpace The address space of the alias pointer.
  /// \param Linkage The linkage type for the alias.
  /// \param Name The name of the alias.
  /// \param Aliasee The global value this alias refers to.
  /// \return The newly created alias.
  LLVM_ABI static GlobalAlias *create(Type *Ty, unsigned AddressSpace,
                                      LinkageTypes Linkage, const Twine &Name,
                                      GlobalValue *Aliasee);

  /// Create a global alias with type, parent, and address space taken from the
  /// aliasee.
  /// \param Linkage The linkage type for the alias.
  /// \param Name The name of the alias.
  /// \param Aliasee The global value this alias refers to.
  /// \return The newly created alias.
  LLVM_ABI static GlobalAlias *create(LinkageTypes Linkage, const Twine &Name,
                                      GlobalValue *Aliasee);

  /// Create a global alias with linkage, type, parent, and address space taken
  /// from the aliasee.
  /// \param Name The name of the alias.
  /// \param Aliasee The global value this alias refers to.
  /// \return The newly created alias.
  LLVM_ABI static GlobalAlias *create(const Twine &Name, GlobalValue *Aliasee);

  /// Allocate a GlobalAlias with space for its one fixed operand.
  /// \param S Allocation size in bytes.
  /// \return A pointer to the allocated GlobalAlias storage.
  void *operator new(size_t S) { return User::operator new(S, AllocMarker); }
  /// Deallocate a GlobalAlias created with the fixed-size allocator.
  /// \param Ptr Pointer returned by the fixed-size \c operator new.
  void operator delete(void *Ptr) { User::operator delete(Ptr, AllocMarker); }

  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \return The operand at the given index.
  inline Constant *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Constant *Val_nocapture);
  /// Return an iterator to the first operand.
  /// \return An iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// \return A const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// \return An iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// \return A const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at a compile-time index.
  /// \return A reference to the operand Use at the compile-time index.
  template <int> inline Use &Op();
  /// Return a const reference to the operand at a compile-time index.
  /// \return A const reference to the operand Use at the compile-time index.
  template <int> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// \return The number of operands.
  inline unsigned getNumOperands() const;

  /// Copy all additional attributes from \p Src to this alias.
  /// \param Src The alias whose attributes are copied.
  void copyAttributesFrom(const GlobalAlias *Src) {
    GlobalValue::copyAttributesFrom(Src);
  }

  /// removeFromParent - This method unlinks 'this' from the containing module,
  /// but does not delete it.
  ///
  LLVM_ABI void removeFromParent();

  /// eraseFromParent - This method unlinks 'this' from the containing module
  /// and deletes it.
  ///
  LLVM_ABI void eraseFromParent();

  /// Set the alias target.
  /// \param Aliasee The constant this alias refers to.
  LLVM_ABI void setAliasee(Constant *Aliasee);
  /// Return the alias target constant.
  /// \return The constant this alias refers to.
  const Constant *getAliasee() const {
    return static_cast<Constant *>(Op<0>().get());
  }
  /// Return the alias target constant.
  /// \return The constant this alias refers to.
  Constant *getAliasee() { return static_cast<Constant *>(Op<0>().get()); }

  /// Return the underlying GlobalObject this alias ultimately refers to.
  /// \return The GlobalObject at the end of the alias chain, or null.
  LLVM_ABI const GlobalObject *getAliaseeObject() const;
  /// Return the underlying GlobalObject this alias ultimately refers to.
  /// \return The GlobalObject at the end of the alias chain, or null.
  GlobalObject *getAliaseeObject() {
    return const_cast<GlobalObject *>(
        static_cast<const GlobalAlias *>(this)->getAliaseeObject());
  }

  /// Return true if \p L is a valid linkage type for a GlobalAlias.
  /// \param L The linkage type to test.
  /// \return True if \p L is valid for a GlobalAlias.
  static bool isValidLinkage(LinkageTypes L) {
    return isExternalLinkage(L) || isLocalLinkage(L) || isWeakLinkage(L) ||
           isLinkOnceLinkage(L) || isAvailableExternallyLinkage(L);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// \return True if \p V is a GlobalAlias.
  static bool classof(const Value *V) {
    return V->getValueID() == Value::GlobalAliasVal;
  }
};

/// Operand layout traits for GlobalAlias.
template <>
struct OperandTraits<GlobalAlias>
    : public FixedNumOperandTraits<GlobalAlias, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(GlobalAlias, Constant)

} // end namespace llvm

#endif // LLVM_IR_GLOBALALIAS_H
