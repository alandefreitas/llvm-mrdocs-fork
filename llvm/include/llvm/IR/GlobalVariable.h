//===-- llvm/GlobalVariable.h - GlobalVariable class ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the GlobalVariable class, which
// represents a single global variable (or constant) in the VM.
//
// Global variables are constant pointers that refer to hunks of space that are
// allocated by either the VM, or by the linker in a static compiler.  A global
// variable may have an initial value, which is copied into the executables .data
// area.  Global Constants are required to have initializers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GLOBALVARIABLE_H
#define LLVM_IR_GLOBALVARIABLE_H

#include "llvm/ADT/Twine.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstddef>

namespace llvm {

class Constant;
class DataLayout;
class Module;

/// Traits class for using GlobalVariable in the Module symbol table.
template <typename ValueSubClass, typename... Args> class SymbolTableListTraits;
class DIGlobalVariableExpression;

/// Represents a single global variable (or constant) in the IR.
///
/// Global variables are constant pointers that refer to hunks of space that are
/// allocated by either the VM, or by the linker in a static compiler. A global
/// variable may have an initial value, which is copied into the executable's
/// .data area. Global constants are required to have initializers.
class GlobalVariable : public GlobalObject, public ilist_node<GlobalVariable> {
  friend class SymbolTableListTraits<GlobalVariable>;

  constexpr static IntrusiveOperandsAllocMarker AllocMarker{1};

  AttributeSet Attrs;

  // Is this a global constant?
  bool isConstantGlobal : 1;
  // Is this a global whose value can change from its initial value before
  // global initializers are run?
  bool isExternallyInitializedConstant : 1;

private:
  static const unsigned CodeModelBits = LastCodeModelBit - LastAlignmentBit;
  static const unsigned CodeModelMask = (1 << CodeModelBits) - 1;
  static const unsigned CodeModelShift = LastAlignmentBit + 1;

public:
  /// Construct a global variable without inserting it into a module.
  ///
  /// \param Ty The value type of the global variable.
  /// \param isConstant Whether the global is a constant.
  /// \param Linkage The linkage type for the global.
  /// \param Initializer The initializer constant, or null for a declaration.
  /// \param Name The name of the global variable.
  /// \param TLMode The thread-local storage mode.
  /// \param AddressSpace The address space of the global pointer.
  /// \param isExternallyInitialized Whether the value may change before global
  ///        initializers run.
  LLVM_ABI GlobalVariable(Type *Ty, bool isConstant, LinkageTypes Linkage,
                          Constant *Initializer = nullptr,
                          const Twine &Name = "",
                          ThreadLocalMode TLMode = NotThreadLocal,
                          unsigned AddressSpace = 0,
                          bool isExternallyInitialized = false);
  /// Construct a global variable and insert it into module \p M.
  ///
  /// The global is inserted before \p InsertBefore if non-null; otherwise it is
  /// appended to the module's global list.
  /// \param M The module to insert the global into.
  /// \param Ty The value type of the global variable.
  /// \param isConstant Whether the global is a constant.
  /// \param Linkage The linkage type for the global.
  /// \param Initializer The initializer constant, or null for a declaration.
  /// \param Name The name of the global variable.
  /// \param InsertBefore The global to insert before, or null to append.
  /// \param TLMode The thread-local storage mode.
  /// \param AddressSpace The address space of the global pointer, or nullopt to
  ///        use the module default.
  /// \param isExternallyInitialized Whether the value may change before global
  ///        initializers run.
  LLVM_ABI GlobalVariable(Module &M, Type *Ty, bool isConstant,
                          LinkageTypes Linkage, Constant *Initializer,
                          const Twine &Name = "",
                          GlobalVariable *InsertBefore = nullptr,
                          ThreadLocalMode TLMode = NotThreadLocal,
                          std::optional<unsigned> AddressSpace = std::nullopt,
                          bool isExternallyInitialized = false);
  /// Copy construction is deleted; GlobalVariable owns module list state.
  /// \param Other The global that would be copied (deleted).
  GlobalVariable(const GlobalVariable &Other) = delete;
  /// Assignment is deleted; GlobalVariable owns module list state.
  /// \param Other The global that would be assigned from (deleted).
  GlobalVariable &operator=(const GlobalVariable &Other) = delete;

private:
  /// Set the number of operands on a GlobalVariable.
  ///
  /// GlobalVariable always allocates space for a single operands, but
  /// doesn't always use it.
  void setGlobalVariableNumOperands(unsigned NumOps) {
    assert(NumOps <= 1 && "GlobalVariable can only have 0 or 1 operands");
    NumUserOperands = NumOps;
  }

public:
  /// Destroy this global variable and drop all references.
  ~GlobalVariable() {
    dropAllReferences();

    // Number of operands can be set to 0 after construction and initialization.
    // Make sure that number of operands is reset to 1, as this is needed in
    // User::operator delete
    setGlobalVariableNumOperands(1);
  }

  /// Allocate a GlobalVariable with space for its one fixed operand.
  /// \param s Allocation size in bytes.
  /// \return A pointer to the allocated GlobalVariable storage.
  void *operator new(size_t s) { return User::operator new(s, AllocMarker); }

  /// Deallocate a GlobalVariable created with the fixed-size allocator.
  /// \param ptr Pointer returned by the fixed-size \c operator new.
  void operator delete(void *ptr) { User::operator delete(ptr, AllocMarker); }

  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \return The operand at the given index.
  inline Value *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Value *Val_nocapture);
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

  /// Definitions have initializers, declarations don't.
  ///
  /// \return True if this global has an initializer.
  inline bool hasInitializer() const { return !isDeclaration(); }

  /// Return true if this global has a definitive initializer.
  ///
  /// Whether the global variable has an initializer, and any other instances of
  /// the global (this can happen due to weak linkage) are guaranteed to have
  /// the same initializer.
  ///
  /// Note that if you want to transform a global, you must use
  /// hasUniqueInitializer() instead, because of the *_odr linkage type.
  ///
  /// Example:
  ///
  /// @a = global SomeType* null - Initializer is both definitive and unique.
  ///
  /// @b = global weak SomeType* null - Initializer is neither definitive nor
  /// unique.
  ///
  /// @c = global weak_odr SomeType* null - Initializer is definitive, but not
  /// unique.
  /// \return True if this global has a definitive initializer.
  inline bool hasDefinitiveInitializer() const {
    return hasInitializer() &&
      // The initializer of a global variable may change to something arbitrary
      // at link time.
      !isInterposable() &&
      // The initializer of a global variable with the externally_initialized
      // marker may change at runtime before C++ initializers are evaluated.
      !isExternallyInitialized();
  }

  /// hasUniqueInitializer - Whether the global variable has an initializer, and
  /// any changes made to the initializer will turn up in the final executable.
  /// \return True if the initializer is unique and safe to modify.
  inline bool hasUniqueInitializer() const {
    return
        // We need to be sure this is the definition that will actually be used
        isStrongDefinitionForLinker() &&
        // It is not safe to modify initializers of global variables with the
        // external_initializer marker since the value may be changed at runtime
        // before C++ initializers are evaluated.
        !isExternallyInitialized();
  }

  /// Return the initializer for this global variable.
  ///
  /// It is illegal to call this method if the global is external, because we
  /// cannot tell what the value is initialized to!
  /// \return The initializer constant for this global.
  inline const Constant *getInitializer() const {
    assert(hasInitializer() && "GV doesn't have initializer!");
    return static_cast<Constant*>(Op<0>().get());
  }
  /// Return the initializer for this global variable.
  /// \return The initializer constant for this global.
  inline Constant *getInitializer() {
    assert(hasInitializer() && "GV doesn't have initializer!");
    return static_cast<Constant*>(Op<0>().get());
  }
  /// Set the initializer for this global variable.
  ///
  /// Removes any existing initializer if InitVal==NULL. The initializer must
  /// have the type getValueType().
  /// \param InitVal The new initializer, or null to clear it.
  LLVM_ABI void setInitializer(Constant *InitVal);

  /// Replace the initializer and update this global's value type.
  ///
  /// Sets the initializer for this global variable, and sets the value type of
  /// the global to the type of the initializer. The initializer must not be
  /// null. This may affect the global's alignment if it isn't explicitly set.
  /// \param InitVal The non-null new initializer.
  LLVM_ABI void replaceInitializer(Constant *InitVal);

  /// Get the size of this global variable in bytes.
  /// This is only a minimum size if this is a declaration or a replaceable
  /// definition.
  /// \param DL The data layout used to compute the size.
  /// \return The size of this global in bytes.
  LLVM_ABI uint64_t getGlobalSize(const DataLayout &DL) const;

  /// Return true if this global is a constant.
  ///
  /// If the value is a global constant, its value is immutable throughout the
  /// runtime execution of the program. Assigning a value into the constant
  /// leads to undefined behavior.
  /// \return True if this global is a constant.
  bool isConstant() const { return isConstantGlobal; }
  /// Set whether this global is a constant.
  /// \param Val True if the global should be treated as a constant.
  void setConstant(bool Val) { isConstantGlobal = Val; }

  /// Return true if this global may change before initializers run.
  /// \return True if the value may change before global initializers run.
  bool isExternallyInitialized() const {
    return isExternallyInitializedConstant;
  }
  /// Set whether this global may change before initializers run.
  /// \param Val True if the value may change before global initializers run.
  void setExternallyInitialized(bool Val) {
    isExternallyInitializedConstant = Val;
  }

  /// Copy additional attributes from \p Src onto this global.
  ///
  /// Copies all additional attributes (those not needed to create a
  /// GlobalVariable) from the GlobalVariable Src to this one.
  /// \param Src The global whose attributes are copied.
  LLVM_ABI void copyAttributesFrom(const GlobalVariable *Src);

  /// removeFromParent - This method unlinks 'this' from the containing module,
  /// but does not delete it.
  ///
  LLVM_ABI void removeFromParent();

  /// eraseFromParent - This method unlinks 'this' from the containing module
  /// and deletes it.
  ///
  LLVM_ABI void eraseFromParent();

  /// Drop all references in preparation to destroy the GlobalVariable. This
  /// drops not only the reference to the initializer but also to any metadata.
  LLVM_ABI void dropAllReferences();

  /// Attach a DIGlobalVariableExpression.
  /// \param GV The debug info expression to attach.
  LLVM_ABI void addDebugInfo(DIGlobalVariableExpression *GV);

  /// Fill the vector with all debug info attachements.
  /// \param GVs The vector to fill with attached debug info expressions.
  LLVM_ABI void
  getDebugInfo(SmallVectorImpl<DIGlobalVariableExpression *> &GVs) const;

  /// Add attribute to this global.
  /// \param Kind The kind of attribute to add.
  void addAttribute(Attribute::AttrKind Kind) {
    Attrs = Attrs.addAttribute(getContext(), Kind);
  }

  /// Add attribute to this global.
  /// \param Kind The attribute name.
  /// \param Val The attribute value, or empty if none.
  void addAttribute(StringRef Kind, StringRef Val = StringRef()) {
    Attrs = Attrs.addAttribute(getContext(), Kind, Val);
  }

  /// Add attributes to this global.
  /// \param AttrBuilder The attributes to add.
  void addAttributes(const AttrBuilder &AttrBuilder) {
    Attrs = Attrs.addAttributes(getContext(), AttrBuilder);
  }

  /// Return true if the attribute exists.
  /// \param Kind The kind of attribute to look up.
  /// \return True if an attribute of the given kind is present.
  bool hasAttribute(Attribute::AttrKind Kind) const {
    return Attrs.hasAttribute(Kind);
  }

  /// Return true if the attribute exists.
  /// \param Kind The attribute name to look up.
  /// \return True if an attribute with the given name is present.
  bool hasAttribute(StringRef Kind) const {
    return Attrs.hasAttribute(Kind);
  }

  /// Return true if any attributes exist.
  /// \return True if this global has any attributes.
  bool hasAttributes() const {
    return Attrs.hasAttributes();
  }

  /// Return the attribute object.
  /// \param Kind The kind of attribute to look up.
  /// \return The attribute of the given kind, if present.
  Attribute getAttribute(Attribute::AttrKind Kind) const {
    return Attrs.getAttribute(Kind);
  }

  /// Return the attribute object.
  /// \param Kind The attribute name to look up.
  /// \return The attribute with the given name, if present.
  Attribute getAttribute(StringRef Kind) const {
    return Attrs.getAttribute(Kind);
  }

  /// Return the attribute set for this global
  /// \return The attribute set attached to this global.
  AttributeSet getAttributes() const {
    return Attrs;
  }

  /// Return attribute set as list with index.
  /// FIXME: This may not be required once ValueEnumerators
  /// in bitcode-writer can enumerate attribute-set.
  /// \param index The attribute list index to associate with this set.
  /// \return An attribute list containing this global's attribute set.
  AttributeList getAttributesAsList(unsigned index) const {
    if (!hasAttributes())
      return AttributeList();
    std::pair<unsigned, AttributeSet> AS[1] = {{index, Attrs}};
    return AttributeList::get(getContext(), AS);
  }

  /// Set attribute list for this global
  /// \param A The attribute set to install on this global.
  void setAttributes(AttributeSet A) {
    Attrs = A;
  }

  /// Check if section name is present
  /// \return True if an implicit section attribute is present.
  bool hasImplicitSection() const {
    if (isDeclarationForLinker())
      return false;
    return getAttributes().hasAttribute("bss-section") ||
           getAttributes().hasAttribute("data-section") ||
           getAttributes().hasAttribute("relro-section") ||
           getAttributes().hasAttribute("rodata-section");
  }

  /// Get the custom code model raw value of this global.
  ///
  /// \return The raw code model bits stored on this global.
  unsigned getCodeModelRaw() const {
    unsigned Data = getGlobalValueSubClassData();
    return (Data >> CodeModelShift) & CodeModelMask;
  }

  /// Get the custom code model of this global if it has one.
  ///
  /// If this global does not have a custom code model, the empty instance
  /// will be returned.
  /// \return The custom code model, or \c std::nullopt if none.
  std::optional<CodeModel::Model> getCodeModel() const {
    unsigned CodeModelData = getCodeModelRaw();
    if (CodeModelData > 0)
      return static_cast<CodeModel::Model>(CodeModelData - 1);
    return {};
  }

  /// Change the code model for this global.
  ///
  /// \param CM The code model to assign.
  LLVM_ABI void setCodeModel(CodeModel::Model CM);

  /// Remove the code model for this global.
  ///
  LLVM_ABI void clearCodeModel();

  /// Returns the alignment of the given variable.
  /// \return The alignment of this global variable, if set.
  MaybeAlign getAlign() const { return GlobalObject::getAlign(); }

  /// Sets the alignment attribute of the GlobalVariable.
  /// \param Align The required alignment.
  void setAlignment(Align Align) { GlobalObject::setAlignment(Align); }

  /// Sets the alignment attribute of the GlobalVariable.
  /// This method will be deprecated as the alignment property should always be
  /// defined.
  /// \param Align The required alignment, or none to clear it.
  void setAlignment(MaybeAlign Align) { GlobalObject::setAlignment(Align); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// \return True if \p V is a GlobalVariable.
  static bool classof(const Value *V) {
    return V->getValueID() == Value::GlobalVariableVal;
  }
};

/// Operand layout traits for GlobalVariable.
template <>
struct OperandTraits<GlobalVariable> :
  public OptionalOperandTraits<GlobalVariable> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(GlobalVariable, Value)

} // end namespace llvm

#endif // LLVM_IR_GLOBALVARIABLE_H
