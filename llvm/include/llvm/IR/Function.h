//===- llvm/Function.h - Class to represent a single function ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Function class, which represents a
// single function/procedure in LLVM.
//
// A function basically consists of a list of basic blocks, a list of arguments,
// and a symbol table.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_FUNCTION_H
#define LLVM_IR_FUNCTION_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace llvm {

namespace Intrinsic {
typedef unsigned ID;
}

class AssemblyAnnotationWriter;
class Constant;
class ConstantRange;
class DataLayout;
struct DenormalFPEnv;
struct DenormalMode;
class DISubprogram;
enum LibFunc : unsigned;
class LLVMContext;
class Module;
class raw_ostream;
class TargetLibraryInfoImpl;
class Type;
class User;
/// Analysis providing relative branch probabilities for CFG edges.
class BranchProbabilityInfo;
class BlockFrequencyInfo;

/// A single function or procedure in LLVM IR.
///
/// A function consists of a list of basic blocks, a list of arguments, and a
/// symbol table.
class LLVM_ABI Function : public GlobalObject, public ilist_node<Function> {
public:
  /// The list type used to store this function's basic blocks.
  using BasicBlockListType = SymbolTableList<BasicBlock>;

  // BasicBlock iterators...
  /// Iterator over this function's basic blocks.
  using iterator = BasicBlockListType::iterator;
  /// Const iterator over this function's basic blocks.
  using const_iterator = BasicBlockListType::const_iterator;

  /// Iterator over this function's formal arguments.
  using arg_iterator = Argument *;
  /// Const iterator over this function's formal arguments.
  using const_arg_iterator = const Argument *;

private:
  constexpr static HungOffOperandsAllocMarker AllocMarker{};

  // Important things that make up a function!
  BasicBlockListType BasicBlocks;         ///< The basic blocks

  // Basic blocks need to get their number when added to a function.
  friend void BasicBlock::setParent(Function *);
  unsigned NextBlockNum = 0;
  /// Epoch of block numbers. (Could be shrinked to uint8_t if required.)
  unsigned BlockNumEpoch = 0;

  mutable Argument *Arguments = nullptr;  ///< The formal arguments
  uint32_t NumArgs;
  MaybeAlign PreferredAlign;
  std::unique_ptr<ValueSymbolTable>
      SymTab;                             ///< Symbol table of args/instructions
  AttributeList AttributeSets;            ///< Parameter attributes

  /*
   * Value::SubclassData
   *
   * bit 0      : HasLazyArguments
   * bit 1      : HasPrefixData
   * bit 2      : HasPrologueData
   * bit 3      : HasPersonalityFn
   * bits 4-13  : CallingConvention
   * bits 14    : HasGC
   * bits 15 : [reserved]
   */

  /// Bits from GlobalObject::GlobalObjectSubclassData.
  enum {
    /// Whether this function is materializable.
    IsMaterializableBit = 0,
  };

  friend class SymbolTableListTraits<Function>;

public:
  /// Return true if the formal argument list has not been built yet.
  ///
  /// The argument list of a function is built on demand, so that the list isn't
  /// allocated until the first client needs it. The hasLazyArguments predicate
  /// returns true if the arg list hasn't been set up yet.
  /// \return True if the argument list has not been built yet.
  bool hasLazyArguments() const {
    return getSubclassDataFromValue() & (1<<0);
  }

  /// Convert all basic blocks in this function to the new debug-info format.
  ///
  /// \see BasicBlock::convertToNewDbgValues.
  void convertToNewDbgValues();

  /// Convert all basic blocks in this function back from the new debug-info format.
  ///
  /// \see BasicBlock::convertFromNewDbgValues.
  void convertFromNewDbgValues();

private:
  friend class TargetLibraryInfoImpl;

  static constexpr LibFunc UnknownLibFunc = LibFunc(-1);

  /// Cache for TLI::getLibFunc() result without prototype validation.
  /// UnknownLibFunc if uninitialized. NotLibFunc if definitely not lib func.
  /// Otherwise may be libfunc if prototype validation passes.
  mutable LibFunc LibFuncCache = UnknownLibFunc;

  void CheckLazyArguments() const {
    if (hasLazyArguments())
      BuildLazyArguments();
  }

  void BuildLazyArguments() const;

  void clearArguments();

  void deleteBodyImpl(bool ShouldDrop);

  /// Function ctor - If the (optional) Module argument is specified, the
  /// function is automatically inserted into the end of the function list for
  /// the module.
  ///
  Function(FunctionType *Ty, LinkageTypes Linkage, unsigned AddrSpace,
           const Twine &N = "", Module *M = nullptr);

public:
  /// Copy construction is deleted; Function owns its basic blocks and state.
  /// \param Other The function that would be copied (deleted).
  Function(const Function &Other) = delete;
  /// Assignment is deleted; Function owns its basic blocks and state.
  /// \param Other The function that would be assigned from (deleted).
  void operator=(const Function &Other) = delete;
  /// Destroy this function and its basic blocks.
  ~Function();

  // This is here to help easily convert from FunctionT * (Function * or
  // MachineFunction *) in BlockFrequencyInfoImpl to Function * by calling
  // FunctionT->getFunction().
  /// Return this function (identity helper for generic FunctionT code).
  /// \return A const reference to this function.
  const Function &getFunction() const { return *this; }

  /// Create a function of type \p Ty with the given linkage and address space.
  ///
  /// If \p M is non-null, the function is inserted into that module.
  /// \param Ty The function type.
  /// \param Linkage The linkage for the new function.
  /// \param AddrSpace The address space of the function pointer.
  /// \param N Optional name for the new function.
  /// \param M Optional module to insert into, or null.
  /// \return The newly created function.
  static Function *Create(FunctionType *Ty, LinkageTypes Linkage,
                          unsigned AddrSpace, const Twine &N = "",
                          Module *M = nullptr) {
    return new (AllocMarker) Function(Ty, Linkage, AddrSpace, N, M);
  }

  // TODO: remove this once all users have been updated to pass an AddrSpace
  /// Create a function of type \p Ty with the given linkage (legacy AddrSpace).
  /// \param Ty The function type.
  /// \param Linkage The linkage for the new function.
  /// \param N Optional name for the new function.
  /// \param M Optional module to insert into, or null.
  /// \return The newly created function.
  static Function *Create(FunctionType *Ty, LinkageTypes Linkage,
                          const Twine &N = "", Module *M = nullptr) {
    return new (AllocMarker)
        Function(Ty, Linkage, static_cast<unsigned>(-1), N, M);
  }

  /// Creates a new function and attaches it to a module.
  ///
  /// Places the function in the program address space as specified
  /// by the module's data layout.
  /// \param Ty The function type.
  /// \param Linkage The linkage for the new function.
  /// \param N The name for the new function.
  /// \param M The module that receives the new function.
  /// \return The newly created function.
  static Function *Create(FunctionType *Ty, LinkageTypes Linkage,
                          const Twine &N, Module &M);

  /// Creates a function with some attributes recorded in llvm.module.flags
  /// and the LLVMContext applied.
  ///
  /// Use this when synthesizing new functions that need attributes that would
  /// have been set by command line options.
  ///
  /// This function should not be called from backends or the LTO pipeline. If
  /// it is called from one of those places, some default attributes will not be
  /// applied to the function.
  /// \param Ty The function type.
  /// \param Linkage The linkage for the new function.
  /// \param AddrSpace The address space of the function pointer.
  /// \param N Optional name for the new function.
  /// \param M Optional module to insert into, or null.
  /// \return The newly created function with default attributes applied.
  static Function *createWithDefaultAttr(FunctionType *Ty, LinkageTypes Linkage,
                                         unsigned AddrSpace,
                                         const Twine &N = "",
                                         Module *M = nullptr);

  // Provide fast operand accessors.
  public:
  /// Return hung-off operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based hung-off operand index.
  /// \return The hung-off operand at the given index.
  inline Value *getOperand(unsigned i_nocapture) const;
  /// Set hung-off operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based hung-off operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Value *Val_nocapture);
  /// Return an iterator to the first hung-off operand.
  /// \return An iterator to the first hung-off operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first hung-off operand.
  /// \return A const iterator to the first hung-off operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last hung-off operand.
  /// \return An iterator past the last hung-off operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last hung-off operand.
  /// \return A const iterator past the last hung-off operand.
  inline const_op_iterator op_end() const;

protected:
  /// Access operand \c Use by compile-time index (negative indexes from the end).
  /// \return A reference to the operand Use at the compile-time index.
  template <int> inline Use &Op();
  /// Access operand \c Use by compile-time index (const overload).
  /// \return A const reference to the operand Use at the compile-time index.
  template <int> inline const Use &Op() const;

public:
  /// Return the number of hung-off operands.
  /// \return The number of hung-off operands.
  inline unsigned getNumOperands() const;

  /// Returns the number of non-debug IR instructions in this function.
  /// This is equivalent to the sum of the sizes of each basic block contained
  /// within this function.
  /// \return The number of non-debug IR instructions in this function.
  unsigned getInstructionCount() const;

  /// Returns the FunctionType for me.
  /// \return The function type of this function.
  FunctionType *getFunctionType() const {
    return cast<FunctionType>(getValueType());
  }

  /// Returns the type of the ret val.
  /// \return The return type of this function.
  Type *getReturnType() const { return getFunctionType()->getReturnType(); }

  /// getContext - Return a reference to the LLVMContext associated with this
  /// function.
  /// \return The LLVMContext associated with this function.
  LLVMContext &getContext() const;

  /// Get the data layout of the module this function belongs to.
  ///
  /// Requires the function to have a parent module.
  /// \return The data layout of the parent module.
  const DataLayout &getDataLayout() const;

  /// isVarArg - Return true if this function takes a variable number of
  /// arguments.
  /// \return True if this function takes a variable number of arguments.
  bool isVarArg() const { return getFunctionType()->isVarArg(); }

  /// Return true if this function's body may be lazily materialized.
  /// \return True if this function's body may be lazily materialized.
  bool isMaterializable() const {
    return getGlobalObjectSubClassData() & (1 << IsMaterializableBit);
  }
  /// Mark whether this function's body may be lazily materialized from bitcode.
  /// \param V True if the function body may be materialized lazily.
  void setIsMaterializable(bool V) {
    unsigned Mask = 1 << IsMaterializableBit;
    setGlobalObjectSubClassData((~Mask & getGlobalObjectSubClassData()) |
                                (V ? Mask : 0u));
  }

  /// Return the intrinsic ID for this function, or not_intrinsic.
  ///
  /// Returns Intrinsic::not_intrinsic if the function is not an intrinsic, or
  /// if the pointer is null. This value is always defined to be zero to allow
  /// easy checking for whether a function is intrinsic or not. The particular
  /// intrinsic functions which correspond to this value are defined in
  /// llvm/Intrinsics.h.
  /// \return The intrinsic ID, or Intrinsic::not_intrinsic if not an intrinsic.
  Intrinsic::ID getIntrinsicID() const LLVM_READONLY { return IntID; }

  /// Return true if this function's name starts with "llvm.".
  ///
  /// It's possible for this function to return true while getIntrinsicID()
  /// returns Intrinsic::not_intrinsic!
  /// \return True if this function's name starts with "llvm.".
  bool isIntrinsic() const { return HasLLVMReservedName; }

  /// Return true if this function is a target-specific intrinsic.
  ///
  /// If this is not an intrinsic or a generic intrinsic, false is returned.
  /// \return True if this function is a target-specific intrinsic.
  bool isTargetIntrinsic() const;

  /// Return true if this is a constrained floating-point intrinsic.
  ///
  /// Returns false when getIntrinsicID() returns Intrinsic::not_intrinsic.
  /// \return True if this is a constrained floating-point intrinsic.
  bool isConstrainedFPIntrinsic() const;

  /// Update internal caches that depend on the function name.
  ///
  /// Note, this method does not need to be called directly, as it is called
  /// from Value::setName() whenever the name of this function changes. Caches
  /// include the intrinsic ID and libcall cache.
  void updateAfterNameChange();

  /// Return the calling convention of this function.
  ///
  /// The enum values for the known calling conventions are defined in
  /// CallingConv.h.
  /// \return The calling convention of this function.
  CallingConv::ID getCallingConv() const {
    return static_cast<CallingConv::ID>((getSubclassDataFromValue() >> 4) &
                                        CallingConv::MaxID);
  }
  /// Set the calling convention of this function.
  /// \param CC The calling convention to set.
  void setCallingConv(CallingConv::ID CC) {
    auto ID = static_cast<unsigned>(CC);
    assert(!(ID & ~CallingConv::MaxID) && "Unsupported calling convention");
    setValueSubclassData((getSubclassDataFromValue() & 0xc00f) | (ID << 4));
  }

  /// Does it have a kernel calling convention?
  /// \return True if this function uses a kernel calling convention.
  bool hasKernelCallingConv() const {
    switch (getCallingConv()) {
    default:
      return false;
    case CallingConv::PTX_Kernel:
    case CallingConv::AMDGPU_KERNEL:
    case CallingConv::SPIR_KERNEL:
      return true;
    }
  }

  /// Set the entry count for this function.
  ///
  /// Entry count is the number of times this function was executed based on
  /// pgo data. \p Imports points to a set of GUIDs that needs to
  /// be imported by the function for sample PGO, to enable the same inlines as
  /// the profiled optimized binary.
  /// \param Count Profiled entry-execution count for this function.
  /// \param Imports Optional set of GUIDs to import for sample PGO, or null.
  void setEntryCount(uint64_t Count,
                     const DenseSet<GlobalValue::GUID> *Imports = nullptr);

  /// Get the entry count for this function.
  ///
  /// Entry count is the number of times the function was executed.
  /// \return The profiled entry count, or std::nullopt if unavailable.
  std::optional<uint64_t> getEntryCount() const;

  /// Return true if the function is annotated with profile data.
  ///
  /// Presence of entry counts from a profile run implies the function has
  /// profile annotations.
  /// \return True if the function has profile annotations.
  bool hasProfileData() const { return getEntryCount().has_value(); }

  /// Returns the set of GUIDs that needs to be imported to the function for
  /// sample PGO, to enable the same inlines as the profiled optimized binary.
  /// \return The set of GUIDs to import for sample PGO.
  DenseSet<GlobalValue::GUID> getImportGUIDs() const;

  /// hasGC/getGC/setGC/clearGC - The name of the garbage collection algorithm
  ///                             to use during code generation.
  /// \return True if this function has a garbage collection algorithm name.
  bool hasGC() const {
    return getSubclassDataFromValue() & (1<<14);
  }
  /// Return the name of the garbage collection algorithm for this function.
  /// \return The garbage collection algorithm name.
  const std::string &getGC() const;
  /// Set the garbage collection algorithm name used during code generation.
  /// \param Str The GC strategy name.
  void setGC(std::string Str);
  /// Remove the garbage-collection algorithm name from this function.
  void clearGC();

  /// Return the attribute list for this Function.
  /// \return The attribute list for this function.
  AttributeList getAttributes() const { return AttributeSets; }

  /// Set the attribute list for this Function.
  /// \param Attrs The complete attribute list to install.
  void setAttributes(AttributeList Attrs) { AttributeSets = Attrs; }

  // TODO: remove non-AtIndex versions of these methods.
  /// adds the attribute to the list of attributes.
  /// \param i AttributeList index (return, function, or parameter).
  /// \param Attr The attribute to add at \p i.
  void addAttributeAtIndex(unsigned i, Attribute Attr);

  /// Add function attributes to this function.
  /// \param Kind The enum attribute kind to add on the function.
  void addFnAttr(Attribute::AttrKind Kind);

  /// Add function attributes to this function.
  /// \param Kind The string attribute kind to add on the function.
  /// \param Val Optional string attribute value.
  void addFnAttr(StringRef Kind, StringRef Val = StringRef());

  /// Add function attributes to this function.
  /// \param Attr The attribute to add on the function.
  void addFnAttr(Attribute Attr);

  /// Add function attributes to this function.
  /// \param Attrs Attributes to add on the function.
  void addFnAttrs(const AttrBuilder &Attrs);

  /// Add return value attributes to this function.
  /// \param Kind The enum attribute kind to add on the return value.
  void addRetAttr(Attribute::AttrKind Kind);

  /// Add return value attributes to this function.
  /// \param Attr The attribute to add on the return value.
  void addRetAttr(Attribute Attr);

  /// Add return value attributes to this function.
  /// \param Attrs Attributes to add on the return value.
  void addRetAttrs(const AttrBuilder &Attrs);

  /// adds the attribute to the list of attributes for the given arg.
  /// \param ArgNo Zero-based index of the function argument.
  /// \param Kind The enum attribute kind to add on the argument.
  void addParamAttr(unsigned ArgNo, Attribute::AttrKind Kind);

  /// adds the attribute to the list of attributes for the given arg.
  /// \param ArgNo Zero-based index of the function argument.
  /// \param Attr The attribute to add on the argument.
  void addParamAttr(unsigned ArgNo, Attribute Attr);

  /// adds the attributes to the list of attributes for the given arg.
  /// \param ArgNo Zero-based index of the function argument.
  /// \param Attrs Attributes to add on the argument.
  void addParamAttrs(unsigned ArgNo, const AttrBuilder &Attrs);

  /// removes the attribute from the list of attributes.
  /// \param i AttributeList index (return, function, or parameter).
  /// \param Kind The enum attribute kind to remove at \p i.
  void removeAttributeAtIndex(unsigned i, Attribute::AttrKind Kind);

  /// removes the attribute from the list of attributes.
  /// \param i AttributeList index (return, function, or parameter).
  /// \param Kind The string attribute kind to remove at \p i.
  void removeAttributeAtIndex(unsigned i, StringRef Kind);

  /// Remove function attributes from this function.
  /// \param Kind The enum attribute kind to remove from the function.
  void removeFnAttr(Attribute::AttrKind Kind);

  /// Remove function attribute from this function.
  /// \param Kind The string attribute kind to remove from the function.
  void removeFnAttr(StringRef Kind);

  /// Remove function attributes matching \p Attrs from this function.
  /// \param Attrs Mask of attribute kinds to remove from the function.
  void removeFnAttrs(const AttributeMask &Attrs);

  /// removes the attribute from the return value list of attributes.
  /// \param Kind The enum attribute kind to remove from the return value.
  void removeRetAttr(Attribute::AttrKind Kind);

  /// removes the attribute from the return value list of attributes.
  /// \param Kind The string attribute kind to remove from the return value.
  void removeRetAttr(StringRef Kind);

  /// removes the attributes from the return value list of attributes.
  /// \param Attrs Mask of attribute kinds to remove from the return value.
  void removeRetAttrs(const AttributeMask &Attrs);

  /// removes the attribute from the list of attributes.
  /// \param ArgNo Zero-based index of the function argument.
  /// \param Kind The enum attribute kind to remove from the argument.
  void removeParamAttr(unsigned ArgNo, Attribute::AttrKind Kind);

  /// removes the attribute from the list of attributes.
  /// \param ArgNo Zero-based index of the function argument.
  /// \param Kind The string attribute kind to remove from the argument.
  void removeParamAttr(unsigned ArgNo, StringRef Kind);

  /// removes the attribute from the list of attributes.
  /// \param ArgNo Zero-based index of the function argument.
  /// \param Attrs Mask of attribute kinds to remove from the argument.
  void removeParamAttrs(unsigned ArgNo, const AttributeMask &Attrs);

  /// Return true if the function has the attribute.
  /// \param Kind The enum attribute kind to query on the function.
  /// \return True if the function has the given attribute.
  bool hasFnAttribute(Attribute::AttrKind Kind) const;

  /// Return true if the function has the attribute.
  /// \param Kind The string attribute kind to query on the function.
  /// \return True if the function has the given attribute.
  bool hasFnAttribute(StringRef Kind) const;

  /// check if an attribute is in the list of attributes for the return value.
  /// \param Kind The enum attribute kind to query on the return value.
  /// \return True if the return value has the given attribute.
  bool hasRetAttribute(Attribute::AttrKind Kind) const;

  /// check if an attributes is in the list of attributes.
  /// \param ArgNo Zero-based index of the function argument.
  /// \param Kind The enum attribute kind to query on the argument.
  /// \return True if the argument has the given attribute.
  bool hasParamAttribute(unsigned ArgNo, Attribute::AttrKind Kind) const;

  /// Check if an attribute is in the list of attributes.
  /// \param ArgNo Zero-based index of the function argument.
  /// \param Kind The string attribute kind to query on the argument.
  /// \return True if the argument has the given attribute.
  bool hasParamAttribute(unsigned ArgNo, StringRef Kind) const;

  /// gets the attribute from the list of attributes.
  /// \param i AttributeList index (return, function, or parameter).
  /// \param Kind The enum attribute kind to look up at \p i.
  /// \return The attribute of the given kind at index \p i.
  Attribute getAttributeAtIndex(unsigned i, Attribute::AttrKind Kind) const;

  /// gets the attribute from the list of attributes.
  /// \param i AttributeList index (return, function, or parameter).
  /// \param Kind The string attribute kind to look up at \p i.
  /// \return The attribute of the given kind at index \p i.
  Attribute getAttributeAtIndex(unsigned i, StringRef Kind) const;

  /// Check if attribute of the given kind is set at the given index.
  /// \param Idx AttributeList index (return, function, or parameter).
  /// \param Kind The enum attribute kind to query at \p Idx.
  /// \return True if the attribute is set at the given index.
  bool hasAttributeAtIndex(unsigned Idx, Attribute::AttrKind Kind) const;

  /// Return the attribute for the given attribute kind.
  /// \param Kind The enum attribute kind to look up on the function.
  /// \return The function attribute of the given kind.
  Attribute getFnAttribute(Attribute::AttrKind Kind) const;

  /// Return the attribute for the given attribute kind.
  /// \param Kind The string attribute kind to look up on the function.
  /// \return The function attribute of the given kind.
  Attribute getFnAttribute(StringRef Kind) const;

  /// Return the attribute for the given attribute kind for the return value.
  /// \param Kind The enum attribute kind to look up on the return value.
  /// \return The return-value attribute of the given kind.
  Attribute getRetAttribute(Attribute::AttrKind Kind) const;

  /// For a string attribute \p Kind, parse attribute as an integer.
  ///
  /// \returns \p Default if attribute is not present.
  ///
  /// \returns \p Default if there is an error parsing the attribute integer,
  /// and error is emitted to the LLVMContext
  /// \param Kind The string attribute kind to parse as an integer.
  /// \param Default Value returned when the attribute is missing or invalid.
  uint64_t getFnAttributeAsParsedInteger(StringRef Kind,
                                         uint64_t Default = 0) const;

  /// gets the specified attribute from the list of attributes.
  /// \param ArgNo Zero-based index of the function argument.
  /// \param Kind The enum attribute kind to look up on the argument.
  /// \return The attribute of the given kind on the argument.
  Attribute getParamAttribute(unsigned ArgNo, Attribute::AttrKind Kind) const;

  /// Return the stack alignment for the function.
  /// \return The stack alignment for the function, if set.
  MaybeAlign getFnStackAlign() const {
    return AttributeSets.getFnStackAlignment();
  }

  /// Returns true if the function has ssp, sspstrong, or sspreq fn attrs.
  /// \return True if the function has a stack-protector function attribute.
  bool hasStackProtectorFnAttr() const;

  /// adds the dereferenceable attribute to the list of attributes for
  /// the given arg.
  /// \param ArgNo Zero-based index of the function argument.
  /// \param Bytes Number of dereferenceable bytes.
  void addDereferenceableParamAttr(unsigned ArgNo, uint64_t Bytes);

  /// adds the dereferenceable_or_null attribute to the list of
  /// attributes for the given arg.
  /// \param ArgNo Zero-based index of the function argument.
  /// \param Bytes Number of dereferenceable-or-null bytes.
  void addDereferenceableOrNullParamAttr(unsigned ArgNo, uint64_t Bytes);

  /// adds the range attribute to the list of attributes for the return value.
  /// \param CR The constant range describing allowed return values.
  void addRangeRetAttr(const ConstantRange &CR);

  /// Return the alignment attribute for parameter \p ArgNo, if any.
  /// \param ArgNo Zero-based index of the function argument.
  /// \return The alignment attribute for the parameter, if any.
  MaybeAlign getParamAlign(unsigned ArgNo) const {
    return AttributeSets.getParamAlignment(ArgNo);
  }

  /// Return the stack alignment attribute for parameter \p ArgNo, if any.
  /// \param ArgNo Zero-based index of the function argument.
  /// \return The stack alignment attribute for the parameter, if any.
  MaybeAlign getParamStackAlign(unsigned ArgNo) const {
    return AttributeSets.getParamStackAlignment(ArgNo);
  }

  /// Extract the byval type for a parameter.
  /// \param ArgNo Zero-based index of the function argument.
  /// \return The byval type for the parameter, or null if none.
  Type *getParamByValType(unsigned ArgNo) const {
    return AttributeSets.getParamByValType(ArgNo);
  }

  /// Extract the sret type for a parameter.
  /// \param ArgNo Zero-based index of the function argument.
  /// \return The sret type for the parameter, or null if none.
  Type *getParamStructRetType(unsigned ArgNo) const {
    return AttributeSets.getParamStructRetType(ArgNo);
  }

  /// Extract the inalloca type for a parameter.
  /// \param ArgNo Zero-based index of the function argument.
  /// \return The inalloca type for the parameter, or null if none.
  Type *getParamInAllocaType(unsigned ArgNo) const {
    return AttributeSets.getParamInAllocaType(ArgNo);
  }

  /// Extract the byref type for a parameter.
  /// \param ArgNo Zero-based index of the function argument.
  /// \return The byref type for the parameter, or null if none.
  Type *getParamByRefType(unsigned ArgNo) const {
    return AttributeSets.getParamByRefType(ArgNo);
  }

  /// Extract the preallocated type for a parameter.
  /// \param ArgNo Zero-based index of the function argument.
  /// \return The preallocated type for the parameter, or null if none.
  Type *getParamPreallocatedType(unsigned ArgNo) const {
    return AttributeSets.getParamPreallocatedType(ArgNo);
  }

  /// Extract the number of dereferenceable bytes for a parameter.
  /// @param ArgNo Index of an argument, with 0 being the first function arg.
  /// \return The number of dereferenceable bytes for the parameter.
  uint64_t getParamDereferenceableBytes(unsigned ArgNo) const {
    return AttributeSets.getParamDereferenceableBytes(ArgNo);
  }

  /// Extract the number of dead_on_return bytes for a parameter.
  /// @param ArgNo Index of an argument, with 0 being the first function arg.
  /// \return Dead-on-return info for the parameter.
  DeadOnReturnInfo getDeadOnReturnInfo(unsigned ArgNo) const {
    return AttributeSets.getDeadOnReturnInfo(ArgNo);
  }

  /// Extract the number of dereferenceable_or_null bytes for a
  /// parameter.
  /// @param ArgNo AttributeList ArgNo, referring to an argument.
  /// \return The number of dereferenceable_or_null bytes for the parameter.
  uint64_t getParamDereferenceableOrNullBytes(unsigned ArgNo) const {
    return AttributeSets.getParamDereferenceableOrNullBytes(ArgNo);
  }

  /// Extract the nofpclass attribute for a parameter.
  /// \param ArgNo Zero-based index of the function argument.
  /// \return The nofpclass test for the parameter.
  FPClassTest getParamNoFPClass(unsigned ArgNo) const {
    return AttributeSets.getParamNoFPClass(ArgNo);
  }

  /// Determine if the function is presplit coroutine.
  /// \return True if the function is a presplit coroutine.
  bool isPresplitCoroutine() const {
    return hasFnAttribute(Attribute::PresplitCoroutine);
  }
  /// Mark this function as a presplit coroutine.
  void setPresplitCoroutine() { addFnAttr(Attribute::PresplitCoroutine); }
  /// Clear the presplit-coroutine attribute after splitting.
  void setSplittedCoroutine() { removeFnAttr(Attribute::PresplitCoroutine); }

  /// Return true if coroutine destroy runs only when the coro is complete.
  /// \return True if coroutine destroy runs only when the coro is complete.
  bool isCoroOnlyDestroyWhenComplete() const {
    return hasFnAttribute(Attribute::CoroDestroyOnlyWhenComplete);
  }
  /// Mark that the coroutine destroy runs only when the coro is complete.
  void setCoroDestroyOnlyWhenComplete() {
    addFnAttr(Attribute::CoroDestroyOnlyWhenComplete);
  }

  /// Return the memory effects attributed to this function.
  /// \return The memory effects attributed to this function.
  MemoryEffects getMemoryEffects() const;
  /// Set the memory effects attributed to this function.
  /// \param ME The memory-effects summary to attach.
  void setMemoryEffects(MemoryEffects ME);

  /// Determine if the function does not access memory.
  /// \return True if the function does not access memory.
  bool doesNotAccessMemory() const;
  /// Mark that the function does not access memory.
  void setDoesNotAccessMemory();

  /// Determine if the function does not access or only reads memory.
  /// \return True if the function does not access or only reads memory.
  bool onlyReadsMemory() const;
  /// Mark that the function does not access or only reads memory.
  void setOnlyReadsMemory();

  /// Determine if the function does not access or only writes memory.
  /// \return True if the function does not access or only writes memory.
  bool onlyWritesMemory() const;
  /// Mark that the function does not access or only writes memory.
  void setOnlyWritesMemory();

  /// Determine if the call can access memory only using pointers based
  /// on its arguments.
  /// \return True if memory access is limited to argument-based pointers.
  bool onlyAccessesArgMemory() const;
  /// Mark that the function may only access memory through its arguments.
  void setOnlyAccessesArgMemory();

  /// Determine if the function may only access memory that is
  ///  inaccessible from the IR.
  /// \return True if the function may only access inaccessible memory.
  bool onlyAccessesInaccessibleMemory() const;
  /// Mark that the function may only access memory inaccessible from the IR.
  void setOnlyAccessesInaccessibleMemory();

  /// Determine if the function may only access memory that is
  ///  either inaccessible from the IR or pointed to by its arguments.
  /// \return True if memory access is limited to inaccessible or argument memory.
  bool onlyAccessesInaccessibleMemOrArgMem() const;
  /// Mark that memory access is limited to inaccessible or argument memory.
  void setOnlyAccessesInaccessibleMemOrArgMem();

  /// Determine if the function cannot return.
  /// \return True if the function cannot return.
  bool doesNotReturn() const {
    return hasFnAttribute(Attribute::NoReturn);
  }
  /// Mark that the function does not return.
  void setDoesNotReturn() {
    addFnAttr(Attribute::NoReturn);
  }

  /// Determine if the function should not perform indirect branch tracking.
  /// \return True if the function should not perform indirect branch tracking.
  bool doesNoCfCheck() const { return hasFnAttribute(Attribute::NoCfCheck); }

  /// Determine if the function cannot unwind.
  /// \return True if the function cannot unwind.
  bool doesNotThrow() const {
    return hasFnAttribute(Attribute::NoUnwind);
  }
  /// Mark that the function does not throw.
  void setDoesNotThrow() {
    addFnAttr(Attribute::NoUnwind);
  }

  /// Determine if the call cannot be duplicated.
  /// \return True if the call cannot be duplicated.
  bool cannotDuplicate() const {
    return hasFnAttribute(Attribute::NoDuplicate);
  }
  /// Mark that the function cannot be duplicated.
  void setCannotDuplicate() {
    addFnAttr(Attribute::NoDuplicate);
  }

  /// Determine if the call is convergent.
  /// \return True if the call is convergent.
  bool isConvergent() const {
    return hasFnAttribute(Attribute::Convergent);
  }
  /// Mark that the function is convergent.
  void setConvergent() {
    addFnAttr(Attribute::Convergent);
  }
  /// Clear the convergent attribute so the function is not treated as convergent.
  void setNotConvergent() {
    removeFnAttr(Attribute::Convergent);
  }

  /// Determine if the call has sideeffects.
  /// \return True if the call is safe to speculate.
  bool isSpeculatable() const {
    return hasFnAttribute(Attribute::Speculatable);
  }
  /// Mark the function as safe to speculate (no side effects or undefined behavior).
  void setSpeculatable() {
    addFnAttr(Attribute::Speculatable);
  }

  /// Determine if the call might deallocate memory.
  /// \return True if the call does not deallocate memory.
  bool doesNotFreeMemory() const {
    return onlyReadsMemory() || hasFnAttribute(Attribute::NoFree);
  }
  /// Mark the function as not deallocating memory (adds \c nofree).
  void setDoesNotFreeMemory() {
    addFnAttr(Attribute::NoFree);
  }

  /// Determine if the call can synchroize with other threads
  /// \return True if the function does not synchronize with other threads.
  bool hasNoSync() const {
    return hasFnAttribute(Attribute::NoSync);
  }
  /// Mark that the function does not synchronize with other threads.
  void setNoSync() {
    addFnAttr(Attribute::NoSync);
  }

  /// Determine if the function is known not to recurse, directly or
  /// indirectly.
  /// \return True if the function is known not to recurse.
  bool doesNotRecurse() const {
    return hasFnAttribute(Attribute::NoRecurse);
  }
  /// Mark that the function does not recurse.
  void setDoesNotRecurse() {
    addFnAttr(Attribute::NoRecurse);
  }

  /// Determine if the function has strict floating point sematics.
  /// \return True if the function has strict floating-point semantics.
  bool isStrictFP() const { return hasFnAttribute(Attribute::StrictFP); }

  /// Determine if the function is required to make forward progress.
  /// \return True if the function must make forward progress.
  bool mustProgress() const {
    return hasFnAttribute(Attribute::MustProgress) ||
           hasFnAttribute(Attribute::WillReturn);
  }
  /// Mark that the function must make forward progress.
  void setMustProgress() { addFnAttr(Attribute::MustProgress); }

  /// Determine if the function will return.
  /// \return True if the function will return.
  bool willReturn() const { return hasFnAttribute(Attribute::WillReturn); }
  /// Mark that the function will return.
  void setWillReturn() { addFnAttr(Attribute::WillReturn); }

  /// Get what kind of unwind table entry to generate for this function.
  /// \return The unwind-table kind for this function.
  UWTableKind getUWTableKind() const {
    return AttributeSets.getUWTableKind();
  }

  /// True if the ABI mandates (or the user requested) that this
  /// function be in a unwind table.
  /// \return True if this function should be in an unwind table.
  bool hasUWTable() const {
    return getUWTableKind() != UWTableKind::None;
  }
  /// Set the unwind-table kind for this function.
  /// \param K The unwind-table kind to request, or None to clear it.
  void setUWTableKind(UWTableKind K) {
    if (K == UWTableKind::None)
      removeFnAttr(Attribute::UWTable);
    else
      addFnAttr(Attribute::getWithUWTableKind(getContext(), K));
  }
  /// True if this function needs an unwind table.
  /// \return True if this function needs an unwind table entry.
  bool needsUnwindTableEntry() const {
    return hasUWTable() || !doesNotThrow() || hasPersonalityFn();
  }

  /// Determine if the function returns a structure through first
  /// or second pointer argument.
  /// \return True if the function has a struct-return attribute.
  bool hasStructRetAttr() const {
    return AttributeSets.hasParamAttr(0, Attribute::StructRet) ||
           AttributeSets.hasParamAttr(1, Attribute::StructRet);
  }

  /// Determine if the parameter or return value is marked with NoAlias
  /// attribute.
  /// \return True if the return value is marked NoAlias.
  bool returnDoesNotAlias() const {
    return AttributeSets.hasRetAttr(Attribute::NoAlias);
  }
  /// Mark that the return value does not alias any other pointer.
  void setReturnDoesNotAlias() { addRetAttr(Attribute::NoAlias); }

  /// Do not optimize this function (-O0).
  /// \return True if this function has the OptimizeNone attribute.
  bool hasOptNone() const { return hasFnAttribute(Attribute::OptimizeNone); }

  /// Determine whether interprocedural transforms may rewrite this function's
  /// signature.
  /// \return True if interprocedural transforms may rewrite the signature.
  bool canChangeSignature() const {
    return !hasFnAttribute(Attribute::Naked) &&
           !hasFnAttribute(Attribute::NoIPA) && !hasOptNone();
  }

  /// Optimize this function for minimum size (-Oz).
  /// \return True if this function is optimized for minimum size.
  bool hasMinSize() const { return hasFnAttribute(Attribute::MinSize); }

  /// Optimize this function for size (-Os) or minimum size (-Oz).
  /// \return True if this function is optimized for size or minimum size.
  bool hasOptSize() const {
    return hasFnAttribute(Attribute::OptimizeForSize) || hasMinSize();
  }

  /// Returns the denormal handling type for the default rounding mode of the
  /// function.
  /// \param FPType The floating-point semantics that select the denormal mode.
  /// \return The denormal handling mode for the given floating-point type.
  DenormalMode getDenormalMode(const fltSemantics &FPType) const;

  /// Return the representational value of the denormal_fpenv attribute.
  /// \return The representational value of the denormal_fpenv attribute.
  DenormalFPEnv getDenormalFPEnv() const;

  /// copyAttributesFrom - copy all additional attributes (those not needed to
  /// create a Function) from the Function Src to this one.
  /// \param Src The function whose attributes are copied.
  void copyAttributesFrom(const Function *Src);

  /// deleteBody - This method deletes the body of the function, and converts
  /// the linkage to external.
  ///
  void deleteBody() {
    deleteBodyImpl(/*ShouldDrop=*/false);
    setLinkage(ExternalLinkage);
  }

  /// removeFromParent - This method unlinks 'this' from the containing module,
  /// but does not delete it.
  ///
  void removeFromParent();

  /// eraseFromParent - This method unlinks 'this' from the containing module
  /// and deletes it.
  ///
  void eraseFromParent();

  /// Steal arguments from another function.
  ///
  /// Drop this function's arguments and splice in the ones from \c Src.
  /// Requires that this has no function body.
  /// \param Src The function whose argument list is taken.
  void stealArgumentListFrom(Function &Src);

  /// Insert \p BB in the basic block list at \p Position. \Returns an iterator
  /// to the newly inserted BB.
  /// \param Position Insertion position in this function's basic-block list.
  /// \param BB The basic block to insert; ownership transfers to this function.
  Function::iterator insert(Function::iterator Position, BasicBlock *BB) {
    Function::iterator FIt = BasicBlocks.insert(Position, BB);
    return FIt;
  }

  /// Transfer all blocks from \p FromF to this function at \p ToIt.
  /// \param ToIt Destination position in this function's basic-block list.
  /// \param FromF The function whose basic blocks are moved.
  void splice(Function::iterator ToIt, Function *FromF) {
    splice(ToIt, FromF, FromF->begin(), FromF->end());
  }

  /// Transfer one BasicBlock from \p FromF at \p FromIt to this function
  /// at \p ToIt.
  /// \param ToIt Destination position in this function's basic-block list.
  /// \param FromF The function that currently owns the block.
  /// \param FromIt Iterator to the single basic block to move.
  void splice(Function::iterator ToIt, Function *FromF,
              Function::iterator FromIt) {
    auto FromItNext = std::next(FromIt);
    // Single-element splice is a noop if destination == source.
    if (ToIt == FromIt || ToIt == FromItNext)
      return;
    splice(ToIt, FromF, FromIt, FromItNext);
  }

  /// Transfer a range of basic blocks that belong to \p FromF from \p
  /// FromBeginIt to \p FromEndIt, to this function at \p ToIt.
  /// \param ToIt Destination position in this function's basic-block list.
  /// \param FromF The function that currently owns the blocks.
  /// \param FromBeginIt Start of the half-open range of blocks to move.
  /// \param FromEndIt End of the half-open range of blocks to move.
  void splice(Function::iterator ToIt, Function *FromF,
              Function::iterator FromBeginIt,
              Function::iterator FromEndIt);

  /// Erases a range of BasicBlocks from \p FromIt to (not including) \p ToIt.
  /// \Returns \p ToIt.
  /// \param FromIt Start of the half-open range of blocks to erase.
  /// \param ToIt End of the half-open range of blocks to erase.
  Function::iterator erase(Function::iterator FromIt, Function::iterator ToIt);

private:
  // These need access to the underlying BB list.
  LLVM_ABI friend void BasicBlock::removeFromParent();
  LLVM_ABI friend iplist<BasicBlock>::iterator BasicBlock::eraseFromParent();
  /// Grant \c InstIterator access to the basic-block list.
  template <class BB_t, class BB_i_t, class BI_t, class II_t>
  friend class InstIterator;
  friend class llvm::SymbolTableListTraits<llvm::BasicBlock>;
  friend class llvm::ilist_node_with_parent<llvm::BasicBlock, llvm::Function>;

  /// Get the underlying elements of the Function... the basic block list is
  /// empty for external functions.
  ///
  /// This is deliberately private because we have implemented an adequate set
  /// of functions to modify the list, including Function::splice(),
  /// Function::erase(), Function::insert() etc.
  const BasicBlockListType &getBasicBlockList() const { return BasicBlocks; }
        BasicBlockListType &getBasicBlockList()       { return BasicBlocks; }

  static BasicBlockListType Function::*getSublistAccess(BasicBlock*) {
    return &Function::BasicBlocks;
  }

public:
  /// Return the entry basic block of this function.
  /// \return The entry basic block of this function.
  const BasicBlock       &getEntryBlock() const   { return front(); }
  /// Return the entry basic block of this function.
  /// \return The entry basic block of this function.
        BasicBlock       &getEntryBlock()         { return front(); }

  //===--------------------------------------------------------------------===//
  // Symbol Table Accessing functions...

  /// Return the value symbol table for this function, or null if none.
  /// \return The value symbol table, or null if none.
  inline ValueSymbolTable *getValueSymbolTable() { return SymTab.get(); }
  /// Return the value symbol table for this function, or null if none.
  /// \return The value symbol table, or null if none.
  inline const ValueSymbolTable *getValueSymbolTable() const {
    return SymTab.get();
  }

  //===--------------------------------------------------------------------===//
  // Block number functions

  /// Return a value larger than the largest block number. Intended to allocate
  /// a vector that is sufficiently large to hold all blocks indexed by their
  /// number.
  /// \return A value larger than the largest basic-block number.
  unsigned getMaxBlockNumber() const { return NextBlockNum; }

  /// Renumber basic blocks into a dense range starting from zero.
  ///
  /// Be aware that other data structures and analyses (e.g., DominatorTree) may
  /// depend on the value numbers and need to be updated or invalidated.
  void renumberBlocks();

  /// Return the epoch of the current basic-block numbering.
  ///
  /// This returns a different value after every renumbering. The intention is:
  /// if something (e.g., an analysis) uses block numbers, it also stores the
  /// number epoch and then can assert later on that the epoch didn't change
  /// (indicating that the numbering is still valid). If the epoch changed,
  /// blocks might have been assigned new numbers and previous uses of the
  /// numbers need to be invalidated. This is solely intended as a debugging
  /// feature.
  /// \return The epoch of the current basic-block numbering.
  unsigned getBlockNumberEpoch() const { return BlockNumEpoch; }

private:
  /// Assert that all blocks have unique numbers within 0..NextBlockNum. This
  /// has O(n) runtime complexity.
  void validateBlockNumbers() const;

public:
  //===--------------------------------------------------------------------===//
  // BasicBlock iterator forwarding functions
  //
  /// Return an iterator to the first basic block in this function.
  /// \return An iterator to the first basic block.
  iterator                begin()       { return BasicBlocks.begin(); }
  /// Return a const iterator to the beginning of this function's basic blocks.
  /// \return A const iterator to the first basic block.
  const_iterator          begin() const { return BasicBlocks.begin(); }
  /// Return an iterator past the last basic block in this function.
  /// \return An iterator past the last basic block.
  iterator                end  ()       { return BasicBlocks.end();   }
  /// Return a const iterator past the last basic block in this function.
  /// \return A const iterator past the last basic block.
  const_iterator          end  () const { return BasicBlocks.end();   }

  /// Return the number of basic blocks in this function.
  /// \return The number of basic blocks in this function.
  size_t                   size() const { return BasicBlocks.size();  }
  /// Return true if this function has no basic blocks.
  /// \return True if this function has no basic blocks.
  bool                    empty() const { return BasicBlocks.empty(); }
  /// Return the first basic block in this function.
  /// \return The first basic block in this function.
  const BasicBlock       &front() const { return BasicBlocks.front(); }
  /// Return the first basic block in this function.
  /// \return The first basic block in this function.
        BasicBlock       &front()       { return BasicBlocks.front(); }
  /// Return the last basic block in this function.
  /// \return The last basic block in this function.
  const BasicBlock        &back() const { return BasicBlocks.back();  }
  /// Return the last basic block in this function.
  /// \return The last basic block in this function.
        BasicBlock        &back()       { return BasicBlocks.back();  }

/// @name Function Argument Iteration
/// @{

  /// Return an iterator to the first formal argument.
  /// \return An iterator to the first formal argument.
  arg_iterator arg_begin() {
    CheckLazyArguments();
    return Arguments;
  }
  /// Return a const iterator to the first formal argument.
  /// \return A const iterator to the first formal argument.
  const_arg_iterator arg_begin() const {
    CheckLazyArguments();
    return Arguments;
  }

  /// Return an iterator past the last formal argument.
  /// \return An iterator past the last formal argument.
  arg_iterator arg_end() {
    CheckLazyArguments();
    return Arguments + NumArgs;
  }
  /// Return a const iterator past the last formal argument.
  /// \return A const iterator past the last formal argument.
  const_arg_iterator arg_end() const {
    CheckLazyArguments();
    return Arguments + NumArgs;
  }

  /// Return the formal argument at zero-based index \p i.
  /// \param i Zero-based index of the formal argument.
  /// \return The formal argument at the given index.
  Argument* getArg(unsigned i) const {
    assert (i < NumArgs && "getArg() out of range!");
    CheckLazyArguments();
    return Arguments + i;
  }

  /// Return a range over this function's formal arguments.
  /// \return A range over this function's formal arguments.
  iterator_range<arg_iterator> args() {
    return make_range(arg_begin(), arg_end());
  }
  /// Return a const range over this function's formal arguments.
  /// \return A const range over this function's formal arguments.
  iterator_range<const_arg_iterator> args() const {
    return make_range(arg_begin(), arg_end());
  }

/// @}

  /// Return the number of formal arguments.
  /// \return The number of formal arguments.
  size_t arg_size() const { return NumArgs; }
  /// Return true if this function has no formal arguments.
  /// \return True if this function has no formal arguments.
  bool arg_empty() const { return arg_size() == 0; }

  /// Check whether this function has a personality function.
  /// \return True if this function has a personality function.
  bool hasPersonalityFn() const {
    return getSubclassDataFromValue() & (1<<3);
  }

  /// Get the personality function associated with this function.
  /// \return The personality function constant, or null if none.
  Constant *getPersonalityFn() const;
  /// Set the personality function used for exception handling in this function.
  /// \param Fn The personality function constant, or null to clear it.
  void setPersonalityFn(Constant *Fn);

  /// Check whether this function has prefix data.
  /// \return True if this function has prefix data.
  bool hasPrefixData() const {
    return getSubclassDataFromValue() & (1<<1);
  }

  /// Get the prefix data associated with this function.
  /// \return The prefix data constant, or null if none.
  Constant *getPrefixData() const;
  /// Set the constant prefix data emitted before this function's entry block.
  /// \param PrefixData The prefix data constant, or null to clear it.
  void setPrefixData(Constant *PrefixData);

  /// Check whether this function has prologue data.
  /// \return True if this function has prologue data.
  bool hasPrologueData() const {
    return getSubclassDataFromValue() & (1<<2);
  }

  /// Get the prologue data associated with this function.
  /// \return The prologue data constant, or null if none.
  Constant *getPrologueData() const;
  /// Set the constant prologue data emitted at the start of this function.
  /// \param PrologueData The prologue data constant, or null to clear it.
  void setPrologueData(Constant *PrologueData);

  /// Print the function to an output stream with an optional
  /// AssemblyAnnotationWriter.
  /// \param OS The output stream.
  /// \param AAW Optional annotation writer, or null.
  /// \param ShouldPreserveUseListOrder Whether to preserve use-list order.
  /// \param IsForDebug Whether to include extra debug formatting.
  void print(raw_ostream &OS, AssemblyAnnotationWriter *AAW = nullptr,
             bool ShouldPreserveUseListOrder = false,
             bool IsForDebug = false) const;

  /// Display this function's CFG in a graph viewer (for debugger use).
  ///
  /// You can say 'call F->viewCFG()' and a ghostview window should pop up from
  /// the program, displaying the CFG of the current function with the code for
  /// each basic block inside. This depends on there being a 'dot' and 'gv'
  /// program in your path.
  void viewCFG() const;

  /// Display this function's CFG, writing the dot graph to \p OutputFileName.
  /// \param OutputFileName Path for the generated dot file.
  void viewCFG(const char *OutputFileName) const;

  /// Display this function's CFG with optional edge weights and output file.
  /// \param ViewCFGOnly If true, omit basic-block contents from graph nodes.
  /// \param BFI Optional block frequency info for edge labels, or null.
  /// \param BPI Optional branch probability info for edge labels, or null.
  /// \param OutputFileName Optional path for the generated dot file, or null.
  void viewCFG(bool ViewCFGOnly, const BlockFrequencyInfo *BFI,
               const BranchProbabilityInfo *BPI,
               const char *OutputFileName = nullptr) const;

  /// Display this function's CFG without basic-block contents (debugger use).
  ///
  /// Works like viewCFG, but nodes show only labels. If you are only interested
  /// in the CFG this can make the graph smaller.
  void viewCFGOnly() const;

  /// Display this function's CFG-only view, writing the dot graph to a file.
  /// \param OutputFileName Path for the generated dot file.
  void viewCFGOnly(const char *OutputFileName) const;

  /// Display this function's CFG-only view with optional edge weight info.
  /// \param BFI Optional block frequency info for edge labels, or null.
  /// \param BPI Optional branch probability info for edge labels, or null.
  void viewCFGOnly(const BlockFrequencyInfo *BFI,
                   const BranchProbabilityInfo *BPI) const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// \return True if \p V is a Function.
  static bool classof(const Value *V) {
    return V->getValueID() == Value::FunctionVal;
  }

  /// Drop all references held by instructions in this function's body.
  ///
  /// This allows one to 'delete' a whole module at a time, even though there
  /// may be circular references: first all references are dropped, and all use
  /// counts go to zero. Then everything is deleted for real. Note that no
  /// operations are valid on an object that has "dropped all references",
  /// except operator delete.
  ///
  /// Since no other object in the module can have references into the body of a
  /// function, dropping all references deletes the entire body of the function,
  /// including any contained basic blocks.
  void dropAllReferences() {
    deleteBodyImpl(/*ShouldDrop=*/true);
  }

  /// Return true if this function has non-call uses that take its address.
  ///
  /// Returns true if there are any uses other than direct calls or invokes to
  /// it, or blockaddress expressions. Optionally passes back an offending user
  /// for diagnostic purposes. Optionally ignores callback uses, assume-like
  /// pointer annotation calls, references in llvm.used and llvm.compiler.used
  /// variables, operand bundle "clang.arc.attachedcall", and direct calls with
  /// a different call site signature (the function is implicitly casted).
  /// \param PutOffender Optional out-parameter for an offending user, or null.
  /// \param IgnoreCallbackUses If true, ignore callback uses.
  /// \param IgnoreAssumeLikeCalls If true, ignore assume-like annotation calls.
  /// \param IngoreLLVMUsed If true, ignore llvm.used / llvm.compiler.used refs.
  /// \param IgnoreARCAttachedCall If true, ignore clang.arc.attachedcall uses.
  /// \param IgnoreCastedDirectCall If true, ignore casted direct calls.
  /// \return True if this function's address is taken by a non-call use.
  bool hasAddressTaken(const User **PutOffender = nullptr,
                       bool IgnoreCallbackUses = false,
                       bool IgnoreAssumeLikeCalls = true,
                       bool IngoreLLVMUsed = false,
                       bool IgnoreARCAttachedCall = false,
                       bool IgnoreCastedDirectCall = false) const;

  /// Return true if this function definition is trivially safe to remove.
  ///
  /// Safe when it is not externally visible, does not have its address taken,
  /// and has no callers. To make this more accurate, call
  /// removeDeadConstantUsers first.
  /// \return True if this function definition is trivially safe to remove.
  bool isDefTriviallyDead() const;

  /// callsFunctionThatReturnsTwice - Return true if the function has a call to
  /// setjmp or other function that gcc recognizes as "returning twice".
  /// \return True if the function calls a returns-twice function such as setjmp.
  bool callsFunctionThatReturnsTwice() const;

  /// Set the attached subprogram.
  ///
  /// Calls \a setMetadata() with \a LLVMContext::MD_dbg.
  /// \param SP The DISubprogram to attach, or null to clear it.
  void setSubprogram(DISubprogram *SP);

  /// Get the attached subprogram.
  ///
  /// Calls \a getMetadata() with \a LLVMContext::MD_dbg and casts the result
  /// to \a DISubprogram.
  /// \return The attached DISubprogram, or null if none.
  DISubprogram *getSubprogram() const;

  /// Returns true if we should emit debug info for profiling.
  /// \return True if debug info for profiling should be emitted.
  bool shouldEmitDebugInfoForProfiling() const;

  /// Return whether null pointer dereference is defined for this function.
  ///
  /// Returns false if null pointer dereference is undefined behavior, and true
  /// if it is not.
  /// \return True if null pointer dereference is defined (not UB) for this function.
  bool nullPointerIsDefined() const;

  /// Returns the alignment of the given function.
  ///
  /// Note that this is the alignment of the code, not the alignment of a
  /// function pointer.
  /// \return The code alignment of this function, if set.
  MaybeAlign getAlign() const { return GlobalObject::getAlign(); }

  /// Sets the alignment attribute of the Function.
  /// \param Align The code alignment to set.
  void setAlignment(Align Align) { GlobalObject::setAlignment(Align); }

  /// Sets the alignment attribute of the Function.
  ///
  /// This method will be deprecated as the alignment property should always be
  /// defined.
  /// \param Align The optional code alignment to set.
  void setAlignment(MaybeAlign Align) { GlobalObject::setAlignment(Align); }

  /// Returns the prefalign of the given function.
  /// \return The preferred alignment of this function, if set.
  MaybeAlign getPreferredAlignment() const { return PreferredAlign; }

  /// Sets the prefalign attribute of the Function.
  /// \param Align The preferred alignment to store.
  void setPreferredAlignment(MaybeAlign Align) { PreferredAlign = Align; }

  /// Return the value for vscale based on the vscale_range attribute or 0 when
  /// unknown.
  /// \return The vscale value from vscale_range, or 0 when unknown.
  unsigned getVScaleValue() const;

private:
  void allocHungoffUselist();
  template<int Idx> void setHungoffOperand(Constant *C);

  /// Shadow Value::setValueSubclassData with a private forwarding method so
  /// that subclasses cannot accidentally use it.
  void setValueSubclassData(unsigned short D) {
    Value::setValueSubclassData(D);
  }
  void setValueSubclassDataBit(unsigned Bit, bool On);
};

namespace CallingConv {

// TODO: Need similar function for support of argument in position. General
// version on FunctionType + Attributes + CallingConv::ID?
/// Return true if \p CC permits a non-void return type.
///
/// Some calling conventions, such as kernel entry points, require void returns.
/// \param CC The calling convention to query.
/// \return True if \p CC permits a non-void return type.
LLVM_ABI LLVM_READNONE bool supportsNonVoidReturnType(CallingConv::ID CC);
} // namespace CallingConv

/// Return whether null pointer dereference is defined for \p F or address space \p AS.
///
/// Null pointer access in a non-zero address space is not considered undefined.
/// Returns false if null pointer dereference is undefined behavior, and true if
/// it is not.
/// \param F The function whose attributes may define null behavior, or null.
/// \param AS The address space to check when \p F is null or does not apply.
/// \return True if null pointer dereference is defined (not UB).
LLVM_ABI bool NullPointerIsDefined(const Function *F, unsigned AS = 0);

/// Operand layout traits for Function (hung-off operands).
template <> struct OperandTraits<Function> : public HungoffOperandTraits {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(Function, Value)

} // end namespace llvm

#endif // LLVM_IR_FUNCTION_H
