//===-------- llvm/GlobalIFunc.h - GlobalIFunc class ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the GlobalIFunc class, which
/// represents a single indirect function in the IR. Indirect function uses
/// ELF symbol type extension to mark that the address of a declaration should
/// be resolved at runtime by calling a resolver function.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GLOBALIFUNC_H
#define LLVM_IR_GLOBALIFUNC_H

#include "llvm/ADT/ilist_node.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Twine;
class Module;

// Traits class for using GlobalIFunc in symbol table in Module.
template <typename ValueSubClass, typename... Args> class SymbolTableListTraits;

/// Represents a single ELF indirect function (ifunc) in the IR.
///
/// An ifunc uses an ELF symbol type extension so that the address of a
/// declaration is resolved at runtime by calling a resolver function.
class GlobalIFunc final : public GlobalObject, public ilist_node<GlobalIFunc> {
  friend class SymbolTableListTraits<GlobalIFunc>;

  constexpr static IntrusiveOperandsAllocMarker AllocMarker{1};

  GlobalIFunc(Type *Ty, unsigned AddressSpace, LinkageTypes Linkage,
              const Twine &Name, Constant *Resolver, Module *Parent);

public:
  /// Copy construction is deleted; GlobalIFunc owns module list state.
  /// \param Other The ifunc that would be copied (deleted).
  GlobalIFunc(const GlobalIFunc &Other) = delete;
  /// Assignment is deleted; GlobalIFunc owns module list state.
  /// \param Other The ifunc that would be assigned from (deleted).
  GlobalIFunc &operator=(const GlobalIFunc &Other) = delete;

  /// If a parent module is specified, the ifunc is automatically inserted into
  /// the end of the specified module's ifunc list.
  /// \param Ty The value type of the ifunc.
  /// \param AddressSpace The address space of the ifunc pointer.
  /// \param Linkage The linkage type for the ifunc.
  /// \param Name The name of the ifunc.
  /// \param Resolver The resolver constant for this ifunc.
  /// \param Parent The parent module, or null to leave the ifunc unattached.
  /// \return The newly created ifunc.
  LLVM_ABI static GlobalIFunc *create(Type *Ty, unsigned AddressSpace,
                                      LinkageTypes Linkage, const Twine &Name,
                                      Constant *Resolver, Module *Parent);

  /// Allocate a GlobalIFunc with space for its one fixed operand.
  /// \param S Allocation size in bytes.
  /// \return A pointer to the allocated GlobalIFunc storage.
  void *operator new(size_t S) { return User::operator new(S, AllocMarker); }
  /// Deallocate a GlobalIFunc created with the fixed-size allocator.
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

  /// Copy all additional attributes from \p Src to this ifunc.
  /// \param Src The ifunc whose attributes are copied.
  void copyAttributesFrom(const GlobalIFunc *Src) {
    GlobalObject::copyAttributesFrom(Src);
  }

  /// This method unlinks 'this' from the containing module, but does not
  /// delete it.
  LLVM_ABI void removeFromParent();

  /// This method unlinks 'this' from the containing module and deletes it.
  LLVM_ABI void eraseFromParent();

  /// Set the ifunc resolver function.
  /// \param Resolver The constant that resolves this ifunc at runtime.
  void setResolver(Constant *Resolver) { Op<0>().set(Resolver); }
  /// Return the ifunc resolver constant.
  /// \return The constant that resolves this ifunc at runtime.
  const Constant *getResolver() const {
    return static_cast<Constant *>(Op<0>().get());
  }
  /// Return the ifunc resolver constant.
  /// \return The constant that resolves this ifunc at runtime.
  Constant *getResolver() { return static_cast<Constant *>(Op<0>().get()); }

  /// Return the resolver function after peeling off potential ConstantExpr
  /// indirection.
  /// \return The resolver function, or null if it cannot be resolved.
  LLVM_ABI const Function *getResolverFunction() const;
  /// Return the resolver function after peeling off potential ConstantExpr
  /// indirection.
  /// \return The resolver function, or null if it cannot be resolved.
  Function *getResolverFunction() {
    return const_cast<Function *>(
        static_cast<const GlobalIFunc *>(this)->getResolverFunction());
  }

  /// Return true if \p L is a valid linkage type for a GlobalIFunc.
  /// \param L The linkage type to test.
  /// \return True if \p L is valid for a GlobalIFunc.
  static bool isValidLinkage(LinkageTypes L) {
    return isExternalLinkage(L) || isLocalLinkage(L) || isWeakLinkage(L) ||
           isLinkOnceLinkage(L);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// \return True if \p V is a GlobalIFunc.
  static bool classof(const Value *V) {
    return V->getValueID() == Value::GlobalIFuncVal;
  }

  /// Apply \p Op to all resolver-related values along the resolution path.
  ///
  /// If the resolver target is already a global object, then apply the
  /// operation to it directly. If the target is a GlobalExpr or a GlobalAlias,
  /// evaluate it to its base object and apply the operation for the base object
  /// and all aliases along the path.
  /// \param Op The operation to apply to each global value on the path.
  LLVM_ABI void
  applyAlongResolverPath(function_ref<void(const GlobalValue &)> Op) const;
};

/// Operand layout traits for GlobalIFunc.
template <>
struct OperandTraits<GlobalIFunc>
    : public FixedNumOperandTraits<GlobalIFunc, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(GlobalIFunc, Constant)

} // end namespace llvm

#endif // LLVM_IR_GLOBALIFUNC_H
