//===- llvm/SymbolTableListTraits.h - Traits for iplist ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a generic class that is used to implement the automatic
// symbol table manipulation that occurs when you put (for example) a named
// instruction into a basic block.
//
// The way that this is implemented is by using a special traits class with the
// intrusive list that makes up the list of instructions in a basic block.  When
// a new element is added to the list of instructions, the traits class is
// notified, allowing the symbol table to be updated.
//
// This generic class implements the traits class.  It must be generic so that
// it can work for all its uses, which include lists of instructions, basic
// blocks, arguments, functions, global variables, etc...
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_SYMBOLTABLELISTTRAITS_H
#define LLVM_IR_SYMBOLTABLELISTTRAITS_H

#include "llvm/ADT/ilist.h"
#include "llvm/ADT/simple_ilist.h"
#include "llvm/Support/Compiler.h"
#include <cstddef>

namespace llvm {

class Argument;
class BasicBlock;
class Function;
class GlobalAlias;
class GlobalIFunc;
class GlobalVariable;
class Instruction;
class Module;
class ValueSymbolTable;

/// Template metafunction to get the parent type for a symbol table list.
///
/// Implementations create a typedef called \c type so that we only need a
/// single template parameter for the list and traits.
template <typename NodeTy> struct SymbolTableListParentType {};

#define DEFINE_SYMBOL_TABLE_PARENT_TYPE(NODE, PARENT)                          \
  template <> struct SymbolTableListParentType<NODE> {                         \
    /** The type of the object that owns a list of this node type. */ \
    using type = PARENT;                                                       \
  };
/// Maps Instruction nodes to their BasicBlock parent.
DEFINE_SYMBOL_TABLE_PARENT_TYPE(Instruction, BasicBlock)
/// Maps BasicBlock nodes to their Function parent.
DEFINE_SYMBOL_TABLE_PARENT_TYPE(BasicBlock, Function)
/// Maps Argument nodes to their Function parent.
DEFINE_SYMBOL_TABLE_PARENT_TYPE(Argument, Function)
/// Maps Function nodes to their Module parent.
DEFINE_SYMBOL_TABLE_PARENT_TYPE(Function, Module)
/// Maps GlobalVariable nodes to their Module parent.
DEFINE_SYMBOL_TABLE_PARENT_TYPE(GlobalVariable, Module)
/// Maps GlobalAlias nodes to their Module parent.
DEFINE_SYMBOL_TABLE_PARENT_TYPE(GlobalAlias, Module)
/// Maps GlobalIFunc nodes to their Module parent.
DEFINE_SYMBOL_TABLE_PARENT_TYPE(GlobalIFunc, Module)
#undef DEFINE_SYMBOL_TABLE_PARENT_TYPE

template <typename NodeTy, typename... Args> class SymbolTableList;

/// Traits that keep parent links and symbol tables in sync with list changes.
///
/// \tparam ValueSubClass The type of objects held in the list, e.g. Instruction.
/// Extra \c Args are forwarded as options to the underlying ilist nodes.
/// The parent type is determined via \a SymbolTableListParentType.
template <typename ValueSubClass, typename... Args>
class SymbolTableListTraits : public ilist_alloc_traits<ValueSubClass> {
  using ListTy = SymbolTableList<ValueSubClass, Args...>;
  using iterator = typename simple_ilist<ValueSubClass, Args...>::iterator;
  using ItemParentClass =
      typename SymbolTableListParentType<ValueSubClass>::type;

public:
  /// Default-construct traits for a symbol table-backed list.
  SymbolTableListTraits() = default;

private:
  /// getListOwner - Return the object that owns this list.  If this is a list
  /// of instructions, it returns the BasicBlock that owns them.
  ItemParentClass *getListOwner() {
    size_t Offset = reinterpret_cast<size_t>(
        &((ItemParentClass *)nullptr->*ItemParentClass::getSublistAccess(
                                           static_cast<ValueSubClass *>(
                                               nullptr))));
    ListTy *Anchor = static_cast<ListTy *>(this);
    return reinterpret_cast<ItemParentClass*>(reinterpret_cast<char*>(Anchor)-
                                              Offset);
  }

  static ListTy &getList(ItemParentClass *Par) {
    return Par->*(Par->getSublistAccess((ValueSubClass*)nullptr));
  }

  static ValueSymbolTable *getSymTab(ItemParentClass *Par) {
    return Par ? toPtr(Par->getValueSymbolTable()) : nullptr;
  }

public:
  /// Add \p V to this list and update its parent and symbol table entry.
  /// @param V Node being inserted into the list.
  void addNodeToList(ValueSubClass *V);
  /// Remove \p V from this list and clear its parent and symbol table entry.
  /// @param V Node being removed from the list.
  void removeNodeFromList(ValueSubClass *V);
  /// Move nodes in [\p first, \p last) from another list into this list.
  /// @param L2 Traits of the list the nodes are transferred from.
  /// @param first Start of the transferred node range.
  /// @param last End of the transferred node range.
  void transferNodesFromList(SymbolTableListTraits &L2, iterator first,
                             iterator last);
  // private:
  /// Reassign the symbol-table owner and migrate named entries between tables.
  ///
  /// Called when the parent of a list changes (for example, a basic block
  /// moves to a different function). Removes named entries from the old
  /// symbol table and reinserts them into the new one.
  /// @param Dest Pointer to the parent/owner field being reassigned.
  /// @param Src New parent/owner value to assign through \p Dest.
  template<typename TPtr>
  void setSymTabObject(TPtr *Dest, TPtr Src);
  /// Identity conversion when the symbol table is already a pointer.
  /// @param P Symbol table pointer to return unchanged.
  /// @return The same symbol table pointer.
  static ValueSymbolTable *toPtr(ValueSymbolTable *P) { return P; }
  /// Address-of conversion when the symbol table is stored by value.
  /// @param R Symbol table reference whose address is returned.
  /// @return A pointer to the symbol table stored by value.
  static ValueSymbolTable *toPtr(ValueSymbolTable &R) { return &R; }
};

// The SymbolTableListTraits template is explicitly instantiated for the
// following data types, so add extern template statements to prevent implicit
// instantiation.
/// Explicit instantiation declaration for BasicBlock list traits.
extern template class LLVM_TEMPLATE_ABI SymbolTableListTraits<BasicBlock>;
/// Explicit instantiation declaration for Function list traits.
extern template class LLVM_TEMPLATE_ABI SymbolTableListTraits<Function>;
/// Explicit instantiation declaration for GlobalAlias list traits.
extern template class LLVM_TEMPLATE_ABI SymbolTableListTraits<GlobalAlias>;
/// Explicit instantiation declaration for GlobalIFunc list traits.
extern template class LLVM_TEMPLATE_ABI SymbolTableListTraits<GlobalIFunc>;
/// Explicit instantiation declaration for GlobalVariable list traits.
extern template class LLVM_TEMPLATE_ABI SymbolTableListTraits<GlobalVariable>;

/// List that automatically updates parent links and symbol tables.
///
/// When nodes are inserted into and removed from this list, the associated
/// symbol table will be automatically updated.  Similarly, parent links get
/// updated automatically.
template <class T, typename... Args>
class SymbolTableList : public iplist_impl<simple_ilist<T, Args...>,
                                           SymbolTableListTraits<T, Args...>> {
};

} // end namespace llvm

#endif // LLVM_IR_SYMBOLTABLELISTTRAITS_H
