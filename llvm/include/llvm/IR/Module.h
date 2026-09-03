//===- llvm/Module.h - C++ class to represent a VM module -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// Module.h This file contains the declarations for the Module class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MODULE_H
#define LLVM_IR_MODULE_H

#include "llvm-c/Types.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class Error;
class FunctionType;
class GVMaterializer;
class LLVMContext;
class MemoryBuffer;
class ModuleSummaryIndex;
class RandomNumberGenerator;
class StructType;
class VersionTuple;

/// Top-level container for LLVM IR objects in a translation unit.
///
/// A Module instance is used to store all the information related to an
/// LLVM module. Modules are the top level container of all other LLVM
/// Intermediate Representation (IR) objects. Each module directly contains a
/// list of globals variables, a list of functions, a list of libraries (or
/// other modules) this module depends on, a symbol table, and various data
/// about the target's characteristics.
///
/// A module maintains a GlobalList object that is used to hold all
/// constant references to global variables in the module.  When a global
/// variable is destroyed, it should have no entries in the GlobalList.
/// The main container class for the LLVM Intermediate Representation.
class LLVM_ABI Module {
  /// @name Types And Enumerations
  /// @{
public:
  /// The type for the list of global variables.
  using GlobalListType = SymbolTableList<GlobalVariable>;
  /// The type for the list of functions.
  using FunctionListType = SymbolTableList<Function>;
  /// The type for the list of aliases.
  using AliasListType = SymbolTableList<GlobalAlias>;
  /// The type for the list of ifuncs.
  using IFuncListType = SymbolTableList<GlobalIFunc>;
  /// The type for the list of named metadata.
  using NamedMDListType = ilist<NamedMDNode>;
  /// The type of the comdat "symbol" table.
  using ComdatSymTabType = StringMap<Comdat>;
  /// The type for mapping names to named metadata.
  using NamedMDSymTabType = StringMap<NamedMDNode *>;

  /// The Global Variable iterator.
  using global_iterator = GlobalListType::iterator;
  /// The Global Variable constant iterator.
  using const_global_iterator = GlobalListType::const_iterator;

  /// The Function iterators.
  using iterator = FunctionListType::iterator;
  /// The Function constant iterator
  using const_iterator = FunctionListType::const_iterator;

  /// The Function reverse iterator.
  using reverse_iterator = FunctionListType::reverse_iterator;
  /// The Function constant reverse iterator.
  using const_reverse_iterator = FunctionListType::const_reverse_iterator;

  /// The Global Alias iterators.
  using alias_iterator = AliasListType::iterator;
  /// The Global Alias constant iterator
  using const_alias_iterator = AliasListType::const_iterator;

  /// The Global IFunc iterators.
  using ifunc_iterator = IFuncListType::iterator;
  /// The Global IFunc constant iterator
  using const_ifunc_iterator = IFuncListType::const_iterator;

  /// The named metadata iterators.
  using named_metadata_iterator = NamedMDListType::iterator;
  /// The named metadata constant iterators.
  using const_named_metadata_iterator = NamedMDListType::const_iterator;

  /// This enumeration defines the supported behaviors of module flags.
  enum ModFlagBehavior {
    /// Emits an error if two values disagree, otherwise the resulting value is
    /// that of the operands.
    Error = 1,

    /// Emits a warning if two values disagree. The result value will be the
    /// operand for the flag from the first module being linked.
    Warning = 2,

    /// Adds a requirement that another module flag be present and have a
    /// specified value after linking is performed. The value must be a metadata
    /// pair, where the first element of the pair is the ID of the module flag
    /// to be restricted, and the second element of the pair is the value the
    /// module flag should be restricted to. This behavior can be used to
    /// restrict the allowable results (via triggering of an error) of linking
    /// IDs with the **Override** behavior.
    Require = 3,

    /// Uses the specified value, regardless of the behavior or value of the
    /// other module. If both modules specify **Override**, but the values
    /// differ, an error will be emitted.
    Override = 4,

    /// Appends the two values, which are required to be metadata nodes.
    Append = 5,

    /// Appends the two values, which are required to be metadata
    /// nodes. However, duplicate entries in the second list are dropped
    /// during the append operation.
    AppendUnique = 6,

    /// Takes the max of the two values, which are required to be integers.
    Max = 7,

    /// Takes the min of the two values, which are required to be integers.
    Min = 8,

    // Markers:
    ModFlagBehaviorFirstVal = Error,
    ModFlagBehaviorLastVal = Min
  };

  /// Checks if Metadata represents a valid ModFlagBehavior, and stores the
  /// converted result in MFB.
  /// \param MD Metadata node to interpret as a behavior.
  /// \param MFB Set to the decoded behavior when conversion succeeds.
  /// @return True if \p MD encodes a valid ModFlagBehavior.
  static bool isValidModFlagBehavior(Metadata *MD, ModFlagBehavior &MFB);

  /// A single module-flag entry: merge behavior, key, and value.
  struct ModuleFlagEntry {
    /// How this flag should be combined when linking modules.
    ModFlagBehavior Behavior;
    /// The flag's string key.
    MDString *Key;
    /// The flag's metadata value.
    Metadata *Val;

    /// Construct a module-flag entry from its behavior, key, and value.
    /// \param B Merge behavior for this flag.
    /// \param K String key identifying the flag.
    /// \param V Metadata value of the flag.
    ModuleFlagEntry(ModFlagBehavior B, MDString *K, Metadata *V)
        : Behavior(B), Key(K), Val(V) {}
  };

  /// Target CPU and feature strings attached to a global asm fragment.
  struct GlobalAsmProperties {
    /// Target feature string for the fragment, or empty.
    std::string TargetFeatures;
    /// Target CPU name for the fragment, or empty.
    std::string TargetCPU;

    /// Set a property using a string name.
    /// Returns whether the property name was valid.
    /// \param Name Property name, such as "target_cpu" or "target_features".
    /// \param Value Property value to store.
    /// @return True if \p Name was a recognized property.
    LLVM_ABI bool set(StringRef Name, std::string Value);

    /// Get a list of set properties as pairs of key and value.
    /// @return Key/value pairs for the properties that are set.
    LLVM_ABI SmallVector<std::pair<StringRef, StringRef>> getAsStrings() const;

    /// Return true if \p Other has the same CPU and feature strings.
    /// \param Other Properties to compare against.
    /// @return True if the properties are equal.
    bool operator==(const GlobalAsmProperties &Other) const {
      return TargetFeatures == Other.TargetFeatures &&
             TargetCPU == Other.TargetCPU;
    }

    /// Return true if \p Other differs in CPU or feature strings.
    /// \param Other Properties to compare against.
    /// @return True if the properties differ.
    bool operator!=(const GlobalAsmProperties &Other) const {
      return !(*this == Other);
    }
  };

  /// A module-scope inline assembly blob with optional target properties.
  struct GlobalAsmFragment {
    /// Assembly text; a trailing newline is enforced on construction.
    std::string Asm;
    /// Target CPU and feature properties for this fragment.
    GlobalAsmProperties Props;

    /// Construct a fragment from assembly text with default properties.
    /// \param Asm Assembly text; a newline is appended if missing.
    GlobalAsmFragment(StringRef Asm) : GlobalAsmFragment(Asm.str()) {}
    /// Construct a fragment from assembly text and target properties.
    /// \param AsmArg Assembly text; a newline is appended if missing.
    /// \param Props Target CPU and feature properties for this fragment.
    GlobalAsmFragment(std::string AsmArg, GlobalAsmProperties Props = {})
        : Asm(std::move(AsmArg)), Props(std::move(Props)) {
      if (!Asm.empty() && Asm.back() != '\n')
        Asm += '\n';
    }

    /// Return true if this fragment has no assembly text.
    /// @return True if the assembly text is empty.
    bool empty() const { return Asm.empty(); }

    /// Return true if \p Other uses the same target properties.
    /// \param Other Fragment whose properties are compared.
    /// @return True if both fragments share the same target properties.
    bool hasSameProperties(const GlobalAsmFragment &Other) const {
      return Props == Other.Props;
    }
  };

/// @}
/// @name Member Variables
/// @{
private:
  LLVMContext &Context;           ///< The LLVMContext from which types and
                                  ///< constants are allocated.
  GlobalListType GlobalList;      ///< The Global Variables in the module
  FunctionListType FunctionList;  ///< The Functions in the module
  AliasListType AliasList;        ///< The Aliases in the module
  IFuncListType IFuncList;        ///< The IFuncs in the module
  NamedMDListType NamedMDList;    ///< The named metadata in the module
  /// Inline Asm at the global scope.
  SmallVector<GlobalAsmFragment, 0> GlobalScopeAsm;
  std::unique_ptr<ValueSymbolTable> ValSymTab; ///< Symbol table for values
  ComdatSymTabType ComdatSymTab;  ///< Symbol table for COMDATs
  std::unique_ptr<MemoryBuffer>
  OwnedMemoryBuffer;              ///< Memory buffer directly owned by this
                                  ///< module, for legacy clients only.
  std::unique_ptr<GVMaterializer>
  Materializer;                   ///< Used to materialize GlobalValues
  std::string ModuleID;           ///< Human readable identifier for the module
  std::string SourceFileName;     ///< Original source file name for module,
                                  ///< recorded in bitcode.
  /// Platform target triple Module compiled on
  /// Format: (arch)(sub)-(vendor)-(sys)-(abi)
  // FIXME: Default construction is not the same as empty triple :(
  Triple TargetTriple = Triple("");
  NamedMDSymTabType NamedMDSymTab;  ///< NamedMDNode names.
  DataLayout DL;                  ///< DataLayout associated with the module
  StringMap<unsigned>
      CurrentIntrinsicIds; ///< Keep track of the current unique id count for
                           ///< the specified intrinsic basename.
  DenseMap<std::pair<Intrinsic::ID, const FunctionType *>, unsigned>
      UniquedIntrinsicNames; ///< Keep track of uniqued names of intrinsics
                             ///< based on unnamed types. The combination of
                             ///< ID and FunctionType maps to the extension that
                             ///< is used to make the intrinsic name unique.

  /// llvm.module.flags metadata
  NamedMDNode *ModuleFlags = nullptr;

  friend class Constant;

/// @}
/// @name Constructors
/// @{
public:
  /// Remove debug intrinsic declarations used only by the old debug-info
  /// format.
  ///
  /// Used when printing this module in the new debug info format; removes all
  /// declarations of debug intrinsics that are replaced by non-intrinsic
  /// records in the new format.
  void removeDebugIntrinsicDeclarations();

  /// Convert debug value intrinsics in every function to DbgRecords.
  ///
  /// \see BasicBlock::convertToNewDbgValues.
  void convertToNewDbgValues() {
    for (auto &F : *this) {
      F.convertToNewDbgValues();
    }

    removeDebugIntrinsicDeclarations();
  }

  /// Convert DbgRecords in every function back to debug value intrinsics.
  ///
  /// \see BasicBlock::convertFromNewDbgValues.
  void convertFromNewDbgValues() {
    for (auto &F : *this) {
      F.convertFromNewDbgValues();
    }
  }

  /// The Module constructor. Note that there is no default constructor. You
  /// must provide a name for the module upon construction.
  /// \param ModuleID Human-readable identifier for the new module.
  /// \param C Context that owns types and constants used by this module.
  explicit Module(StringRef ModuleID, LLVMContext& C);
  /// The module destructor. This will dropAllReferences.
  ~Module();

  /// Move assignment.
  /// \param Other Module to move from.
  /// @return A reference to this module after the move.
  Module &operator=(Module &&Other);

  /// @}
  /// @name Module Level Accessors
  /// @{

  /// Get the module identifier which is, essentially, the name of the module.
  /// @returns the module identifier as a string
  const std::string &getModuleIdentifier() const { return ModuleID; }

  /// Returns the number of non-debug IR instructions in this module.
  ///
  /// This is equivalent to the sum of the IR instruction counts of each
  /// function contained in the module.
  /// @return The total non-debug IR instruction count.
  unsigned getInstructionCount() const;

  /// Get the module's original source file name.
  ///
  /// When compiling from bitcode, this is taken from a bitcode record where it
  /// was recorded. For other compiles it is the same as the ModuleID, which
  /// would contain the source file name.
  /// @return The original source file name for this module.
  const std::string &getSourceFileName() const { return SourceFileName; }

  /// Get a short "name" for the module.
  ///
  /// This is useful for debugging or logging. It is essentially a convenience
  /// wrapper around getModuleIdentifier().
  /// @return The module identifier as a StringRef.
  StringRef getName() const { return ModuleID; }

  /// Get the data layout string for the module's target platform. This is
  /// equivalent to getDataLayout()->getStringRepresentation().
  /// @return The data layout string representation.
  const std::string &getDataLayoutStr() const {
    return DL.getStringRepresentation();
  }

  /// Get the data layout for the module's target platform.
  /// @return The DataLayout associated with this module.
  const DataLayout &getDataLayout() const { return DL; }

  /// Get the target triple which is a string describing the target host.
  /// @return The target triple for this module.
  const Triple &getTargetTriple() const { return TargetTriple; }

  /// Get the global data context.
  /// @returns LLVMContext - a container for LLVM's global information
  LLVMContext &getContext() const { return Context; }

  /// Get any module-scope inline assembly blocks.
  /// @return A const view of the module-scope inline assembly fragments.
  ArrayRef<GlobalAsmFragment> getModuleInlineAsm() const {
    return GlobalScopeAsm;
  }

  /// Get any module-scope inline assembly blocks.
  /// @return A mutable view of the module-scope inline assembly fragments.
  MutableArrayRef<GlobalAsmFragment> getModuleInlineAsm() {
    return GlobalScopeAsm;
  }

  /// Return whether there is any module-scope inline assembly.
  /// @return True if this module has any module-scope inline assembly.
  bool hasModuleInlineAsm() const { return !GlobalScopeAsm.empty(); }

  /// Get a RandomNumberGenerator salted for use with this module.
  ///
  /// The RNG can be seeded via -rng-seed=<uint64> and is salted with the
  /// ModuleID and the provided pass salt. The returned RNG should not
  /// be shared across threads or passes.
  ///
  /// A unique RNG per pass ensures a reproducible random stream even
  /// when other randomness consuming passes are added or removed. In
  /// addition, the random stream will be reproducible across LLVM
  /// versions when the pass does not change.
  /// \param Name Pass-specific salt mixed into the generator.
  /// @return A new RNG salted with this module and \p Name.
  std::unique_ptr<RandomNumberGenerator> createRNG(const StringRef Name) const;

  /// Return true if size-info optimization remark is enabled, false
  /// otherwise.
  /// @return True if size-info analysis remarks are enabled.
  bool shouldEmitInstrCountChangedRemark() {
    return getContext().getDiagHandlerPtr()->isAnalysisRemarkEnabled(
        "size-info");
  }

  /// @}
  /// @name Module Level Mutators
  /// @{

  /// Set the module identifier.
  /// \param ID Human-readable identifier for this module.
  void setModuleIdentifier(StringRef ID) { ModuleID = std::string(ID); }

  /// Set the module's original source file name.
  /// \param Name Original source file name to record.
  void setSourceFileName(StringRef Name) { SourceFileName = std::string(Name); }

  /// Set the data layout
  /// \param Desc Data layout string for the target.
  void setDataLayout(StringRef Desc);
  /// Set the data layout from an existing DataLayout.
  /// \param Other Data layout to copy into this module.
  void setDataLayout(const DataLayout &Other);

  /// Set the target triple.
  /// \param T Target triple this module is compiled for.
  void setTargetTriple(Triple T) { TargetTriple = std::move(T); }

  /// Remove all module-scope inline assembly.
  void removeModuleInlineAsm() { GlobalScopeAsm.clear(); }

  /// Set the module-scope inline assembly blocks.
  /// A trailing newline is added if the input doesn't have one.
  /// \param Fragment Inline assembly fragment that replaces existing asm.
  void setModuleInlineAsm(GlobalAsmFragment Fragment) {
    GlobalScopeAsm.clear();
    appendModuleInlineAsm(std::move(Fragment));
  }

  /// Replace module-scope inline assembly with the given fragments.
  /// \param Fragments Inline assembly fragments that replace existing asm.
  void setModuleInlineAsm(ArrayRef<GlobalAsmFragment> Fragments) {
    GlobalScopeAsm.clear();
    append_range(GlobalScopeAsm, Fragments);
  }

  /// Append to the module-scope inline assembly blocks.
  /// A trailing newline is added if the input doesn't have one.
  /// \param Fragment Inline assembly fragment to append.
  void appendModuleInlineAsm(GlobalAsmFragment Fragment) {
    if (Fragment.empty())
      return;

    if (!GlobalScopeAsm.empty() &&
        GlobalScopeAsm.back().hasSameProperties(Fragment)) {
      GlobalScopeAsm.back().Asm += Fragment.Asm;
    } else {
      GlobalScopeAsm.emplace_back(std::move(Fragment));
    }
  }

  /// Prepend to the module-scope inline assembly blocks.
  /// \param Fragment Inline assembly fragment to prepend.
  void prependModuleInlineAsm(GlobalAsmFragment Fragment) {
    if (Fragment.empty())
      return;

    if (!GlobalScopeAsm.empty() &&
        GlobalScopeAsm.front().hasSameProperties(Fragment)) {
      GlobalScopeAsm.front().Asm.insert(0, Fragment.Asm);
    } else {
      GlobalScopeAsm.insert(GlobalScopeAsm.begin(), std::move(Fragment));
    }
  }

/// @}
/// @name Generic Value Accessors
/// @{

  /// Return the global value in the module with the specified name, of
  /// arbitrary type. This method returns null if a global with the specified
  /// name is not found.
  /// \param Name Global value name to look up.
  /// @return The GlobalValue named \p Name, or null if not found.
  GlobalValue *getNamedValue(StringRef Name) const;

  /// Return the number of global values in the module.
  /// @return The number of named global values.
  unsigned getNumNamedValues() const;

  /// Return a unique non-zero ID for the specified metadata kind. This ID is
  /// uniqued across modules in the current LLVMContext.
  /// \param Name Metadata kind name to intern.
  /// @return A unique non-zero metadata kind ID for \p Name.
  unsigned getMDKindID(StringRef Name) const;

  /// Populate client supplied SmallVector with the name for custom metadata IDs
  /// registered in this LLVMContext.
  /// \param Result Output list of registered metadata kind names.
  void getMDKindNames(SmallVectorImpl<StringRef> &Result) const;

  /// Populate client supplied SmallVector with the bundle tags registered in
  /// this LLVMContext.  The bundle tags are ordered by increasing bundle IDs.
  /// \see LLVMContext::getOperandBundleTagID
  /// \param Result Output list of operand-bundle tag names.
  void getOperandBundleTags(SmallVectorImpl<StringRef> &Result) const;

  /// Return the identified (non-literal) struct types used in this module.
  /// @return The identified struct types referenced by this module.
  std::vector<StructType *> getIdentifiedStructTypes() const;

  /// Return a unique name for an intrinsic whose mangling is based on an
  /// unnamed type. The Proto represents the function prototype.
  /// \param BaseName Intrinsic basename, such as "llvm.foo".
  /// \param Id Intrinsic ID used when uniquing names for \p Proto.
  /// \param Proto Function type that distinguishes this intrinsic overload.
  /// @return A unique intrinsic name string for this overload.
  std::string getUniqueIntrinsicName(StringRef BaseName, Intrinsic::ID Id,
                                     const FunctionType *Proto);

/// @}
/// @name Function Accessors
/// @{

  /// Look up a function by name, inserting a prototype if it is missing.
  ///
  /// Look up the specified function in the module symbol table. If it does not
  /// exist, add a prototype for the function and return it. Otherwise, return
  /// the existing function.
  ///
  /// In all cases, the returned value is a FunctionCallee wrapper around the
  /// 'FunctionType *T' passed in, as well as the 'Value*' of the Function. The
  /// function type of the function may differ from the function type stored in
  /// FunctionCallee if it was previously created with a different type.
  ///
  /// Note: For library calls getOrInsertLibFunc() should be used instead.
  /// \param Name Function name to look up or create.
  /// \param T Function type used for the callee wrapper and any new prototype.
  /// \param AttributeList Attributes applied when a new function is created.
  /// @return A FunctionCallee wrapping the looked-up or newly created function.
  FunctionCallee getOrInsertFunction(StringRef Name, FunctionType *T,
                                     AttributeList AttributeList);

  /// Look up a function by name, inserting an unattributed prototype if
  /// missing.
  /// \param Name Function name to look up or create.
  /// \param T Function type used for the callee wrapper and any new prototype.
  /// @return A FunctionCallee for the looked-up or newly created function.
  FunctionCallee getOrInsertFunction(StringRef Name, FunctionType *T);

  /// Same as above, but takes a list of function arguments, which makes it
  /// easier for clients to use.
  /// \param Name Function name to look up or create.
  /// \param AttributeList Attributes applied when a new function is created.
  /// \param RetTy Return type of the constructed function type.
  /// \param Args Parameter types of the constructed function type.
  /// @return A FunctionCallee for the looked-up or newly created function.
  template <typename... ArgsTy>
  FunctionCallee getOrInsertFunction(StringRef Name,
                                     AttributeList AttributeList, Type *RetTy,
                                     ArgsTy... Args) {
    SmallVector<Type*, sizeof...(ArgsTy)> ArgTys{Args...};
    return getOrInsertFunction(Name,
                               FunctionType::get(RetTy, ArgTys, false),
                               AttributeList);
  }

  /// Same as above, but without the attributes.
  /// \param Name Function name to look up or create.
  /// \param RetTy Return type of the constructed function type.
  /// \param Args Parameter types of the constructed function type.
  /// @return A FunctionCallee for the looked-up or newly created function.
  template <typename... ArgsTy>
  FunctionCallee getOrInsertFunction(StringRef Name, Type *RetTy,
                                     ArgsTy... Args) {
    return getOrInsertFunction(Name, AttributeList{}, RetTy, Args...);
  }

  // Avoid an incorrect ordering that'd otherwise compile incorrectly.
  /// Deleted overload that rejects a FunctionType in the attribute-list slot.
  /// \param Name Unused; this overload is deleted.
  /// \param AttributeList Unused; this overload is deleted.
  /// \param Invalid Unused; this overload is deleted.
  /// \param Args Unused; this overload is deleted.
  template <typename... ArgsTy>
  FunctionCallee
  getOrInsertFunction(StringRef Name, AttributeList AttributeList,
                      FunctionType *Invalid, ArgsTy... Args) = delete;

  /// Look up the specified function in the module symbol table. If it does not
  /// exist, return null.
  /// \param Name Function name to look up.
  /// @return The Function named \p Name, or null if not found.
  Function *getFunction(StringRef Name) const;

/// @}
/// @name Global Variable Accessors
/// @{

  /// Look up a global variable by name, ignoring internal linkage.
  ///
  /// Look up the specified global variable in the module symbol table. If it
  /// does not exist, return null. If AllowInternal is set to true, this
  /// function will return types that have InternalLinkage. By default, these
  /// types are not returned.
  /// \param Name Global variable name to look up.
  /// @return The GlobalVariable named \p Name, or null if not found.
  GlobalVariable *getGlobalVariable(StringRef Name) const {
    return getGlobalVariable(Name, false);
  }

  /// Look up a global variable by name, optionally including internal linkage.
  /// \param Name Global variable name to look up.
  /// \param AllowInternal True to also return internally-linked variables.
  /// @return The GlobalVariable named \p Name, or null if not found.
  GlobalVariable *getGlobalVariable(StringRef Name, bool AllowInternal) const;

  /// Look up a global variable by name, optionally including internal linkage.
  /// \param Name Global variable name to look up.
  /// \param AllowInternal True to also return internally-linked variables.
  /// @return The GlobalVariable named \p Name, or null if not found.
  GlobalVariable *getGlobalVariable(StringRef Name,
                                    bool AllowInternal = false) {
    return static_cast<const Module *>(this)->getGlobalVariable(Name,
                                                                AllowInternal);
  }

  /// Return the global variable in the module with the specified name, of
  /// arbitrary type. This method returns null if a global with the specified
  /// name is not found.
  /// \param Name Global variable name to look up, including internals.
  /// @return The GlobalVariable named \p Name, or null if not found.
  const GlobalVariable *getNamedGlobal(StringRef Name) const {
    return getGlobalVariable(Name, true);
  }
  /// Return the named global variable, including those with internal linkage.
  /// \param Name Global variable name to look up, including internals.
  /// @return The GlobalVariable named \p Name, or null if not found.
  GlobalVariable *getNamedGlobal(StringRef Name) {
    return const_cast<GlobalVariable *>(
                       static_cast<const Module *>(this)->getNamedGlobal(Name));
  }

  /// Look up the specified global in the module symbol table.
  /// If it does not exist, invoke a callback to create a declaration of the
  /// global and return it.
  /// \param Name Global name to look up or create.
  /// \param Ty Element type of the global variable.
  /// \param CreateGlobalCallback Called to create the global when missing.
  /// @return The existing or callback-created GlobalVariable for \p Name.
  GlobalVariable *
  getOrInsertGlobal(StringRef Name, Type *Ty,
                    function_ref<GlobalVariable *()> CreateGlobalCallback);

  /// Look up the specified global in the module symbol table. If required, this
  /// overload constructs the global variable using its constructor's defaults.
  /// \param Name Global name to look up or create.
  /// \param Ty Element type of the global variable.
  /// @return The existing or newly created GlobalVariable for \p Name.
  GlobalVariable *getOrInsertGlobal(StringRef Name, Type *Ty);

/// @}
/// @name Global Alias Accessors
/// @{

  /// Return the global alias in the module with the specified name, of
  /// arbitrary type. This method returns null if a global with the specified
  /// name is not found.
  /// \param Name Alias name to look up.
  /// @return The GlobalAlias named \p Name, or null if not found.
  GlobalAlias *getNamedAlias(StringRef Name) const;

/// @}
/// @name Global IFunc Accessors
/// @{

  /// Return the global ifunc in the module with the specified name, of
  /// arbitrary type. This method returns null if a global with the specified
  /// name is not found.
  /// \param Name IFunc name to look up.
  /// @return The GlobalIFunc named \p Name, or null if not found.
  GlobalIFunc *getNamedIFunc(StringRef Name) const;

/// @}
/// @name Named Metadata Accessors
/// @{

  /// Return the first NamedMDNode in the module with the specified name. This
  /// method returns null if a NamedMDNode with the specified name is not found.
  /// \param Name Named metadata name to look up.
  /// @return The NamedMDNode named \p Name, or null if not found.
  NamedMDNode *getNamedMetadata(StringRef Name) const;

  /// Return the named MDNode in the module with the specified name. This method
  /// returns a new NamedMDNode if a NamedMDNode with the specified name is not
  /// found.
  /// \param Name Named metadata name to look up or create.
  /// @return The existing or newly created NamedMDNode for \p Name.
  NamedMDNode *getOrInsertNamedMetadata(StringRef Name);

  /// Remove the given NamedMDNode from this module and delete it.
  /// \param NMD Named metadata node to erase.
  void eraseNamedMetadata(NamedMDNode *NMD);

/// @}
/// @name Comdat Accessors
/// @{

  /// Return the Comdat in the module with the specified name. It is created
  /// if it didn't already exist.
  /// \param Name Comdat name to look up or create.
  /// @return The existing or newly created Comdat for \p Name.
  Comdat *getOrInsertComdat(StringRef Name);

/// @}
/// @name Module Flags Accessors
/// @{

  /// Returns the module flags in the provided vector.
  /// \param Flags Output vector filled with module-flag entries.
  void getModuleFlagsMetadata(SmallVectorImpl<ModuleFlagEntry> &Flags) const;

  /// Return the corresponding value if Key appears in module flags, otherwise
  /// return null.
  /// \param Key String key of the flag to look up.
  /// @return The flag metadata value, or null if \p Key is absent.
  Metadata *getModuleFlag(StringRef Key) const;

  /// Returns the NamedMDNode in the module that represents module-level flags.
  /// This method returns null if there are no module-level flags.
  /// @return The module-flags NamedMDNode, or null if none exist.
  NamedMDNode *getModuleFlagsMetadata() const { return ModuleFlags; }

  /// Returns the NamedMDNode in the module that represents module-level flags.
  /// If module-level flags aren't found, it creates the named metadata that
  /// contains them.
  /// @return The module-flags NamedMDNode, creating it if needed.
  NamedMDNode *getOrInsertModuleFlagsMetadata();

  /// Add a module-level flag to the module-level flags metadata. It will create
  /// the module-level flags named metadata if it doesn't already exist.
  /// \param Behavior How this flag should be combined when linking.
  /// \param Key String key identifying the flag.
  /// \param Val Metadata value of the flag.
  void addModuleFlag(ModFlagBehavior Behavior, StringRef Key, Metadata *Val);
  /// Add a module flag whose value is constant metadata.
  /// \param Behavior How this flag should be combined when linking.
  /// \param Key String key identifying the flag.
  /// \param Val Constant wrapped as the flag value.
  void addModuleFlag(ModFlagBehavior Behavior, StringRef Key, Constant *Val);
  /// Add a module flag whose value is a 64-bit integer.
  /// \param Behavior How this flag should be combined when linking.
  /// \param Key String key identifying the flag.
  /// \param Val Integer value stored as the flag.
  void addModuleFlag(ModFlagBehavior Behavior, StringRef Key, uint64_t Val);
  /// Add a module flag whose value is a 32-bit integer.
  /// \param Behavior How this flag should be combined when linking.
  /// \param Key String key identifying the flag.
  /// \param Val Integer value stored as the flag.
  void addModuleFlag(ModFlagBehavior Behavior, StringRef Key, uint32_t Val);
  /// Add a module flag whose value is a signed integer stored as uint32_t.
  /// \param Behavior How this flag should be combined when linking.
  /// \param Key String key identifying the flag.
  /// \param Val Integer value stored as the flag.
  inline void addModuleFlag(ModFlagBehavior Behavior, StringRef Key, int Val) {
    addModuleFlag(Behavior, Key, static_cast<uint32_t>(Val));
  }
  /// Add a prebuilt three-operand module-flag metadata node.
  /// \param Node Metadata node with behavior, key, and value operands.
  void addModuleFlag(MDNode *Node);
  /// Like addModuleFlag but replaces the old module flag if it already exists.
  /// \param Behavior How this flag should be combined when linking.
  /// \param Key String key identifying the flag.
  /// \param Val Metadata value of the flag.
  void setModuleFlag(ModFlagBehavior Behavior, StringRef Key, Metadata *Val);
  /// Set a module flag whose value is constant metadata, replacing any
  /// existing flag with the same key.
  /// \param Behavior How this flag should be combined when linking.
  /// \param Key String key identifying the flag.
  /// \param Val Constant wrapped as the flag value.
  void setModuleFlag(ModFlagBehavior Behavior, StringRef Key, Constant *Val);
  /// Set a module flag whose value is a 64-bit integer, replacing any existing
  /// flag with the same key.
  /// \param Behavior How this flag should be combined when linking.
  /// \param Key String key identifying the flag.
  /// \param Val Integer value stored as the flag.
  void setModuleFlag(ModFlagBehavior Behavior, StringRef Key, uint64_t Val);
  /// Set a module flag whose value is a 32-bit integer, replacing any existing
  /// flag with the same key.
  /// \param Behavior How this flag should be combined when linking.
  /// \param Key String key identifying the flag.
  /// \param Val Integer value stored as the flag.
  void setModuleFlag(ModFlagBehavior Behavior, StringRef Key, uint32_t Val);
  /// Set a module flag whose value is a signed integer stored as uint32_t.
  /// \param Behavior How this flag should be combined when linking.
  /// \param Key String key identifying the flag.
  /// \param Val Integer value stored as the flag.
  inline void setModuleFlag(ModFlagBehavior Behavior, StringRef Key, int Val) {
    setModuleFlag(Behavior, Key, static_cast<uint32_t>(Val));
  }

  /// @}
  /// @name Materialization
  /// @{

  /// Set the GVMaterializer used to lazily load GlobalValues.
  ///
  /// This module must not yet have a Materializer. To reset the materializer
  /// for a module that already has one, call materializeAll first. Destroying
  /// this module will destroy its materializer without materializing any more
  /// GlobalValues. Without destroying the Module, there is no way to detach or
  /// destroy a materializer without materializing all the GVs it controls, to
  /// avoid leaving orphan unmaterialized GVs.
  /// \param GVM Materializer this module should take ownership of.
  void setMaterializer(GVMaterializer *GVM);
  /// Retrieves the GVMaterializer, if any, for this Module.
  /// @return The materializer, or null if none is set.
  GVMaterializer *getMaterializer() const { return Materializer.get(); }
  /// Return true if this module has no pending materializer.
  /// @return True if all GlobalValues are fully materialized.
  bool isMaterialized() const { return !getMaterializer(); }

  /// Make sure the GlobalValue is fully read.
  /// \param GV Global value to materialize.
  /// @return Success, or an error if materialization fails.
  llvm::Error materialize(GlobalValue *GV);

  /// Make sure all GlobalValues in this Module are fully read and clear the
  /// Materializer.
  /// @return Success, or an error if materialization fails.
  llvm::Error materializeAll();

  /// Materialize metadata for this module if a materializer is present.
  /// @return Success, or an error if materialization fails.
  llvm::Error materializeMetadata();

  /// Detach global variable \p GV from the list but don't delete it.
  /// \param GV Global variable to unlink.
  void removeGlobalVariable(GlobalVariable *GV) { GlobalList.remove(GV); }
  /// Remove global variable \p GV from the list and delete it.
  /// \param GV Global variable to erase.
  void eraseGlobalVariable(GlobalVariable *GV) { GlobalList.erase(GV); }
  /// Insert global variable \p GV at the end of the global variable list and
  /// take ownership.
  /// \param GV Global variable to insert.
  void insertGlobalVariable(GlobalVariable *GV) {
    insertGlobalVariable(GlobalList.end(), GV);
  }
  /// Insert global variable \p GV into the global variable list before \p
  /// Where and take ownership.
  /// \param Where Insertion point in the global variable list.
  /// \param GV Global variable to insert.
  void insertGlobalVariable(GlobalListType::iterator Where, GlobalVariable *GV) {
    GlobalList.insert(Where, GV);
  }
  // Use global_size() to get the total number of global variables.
  // Use globals() to get the range of all global variables.

  /// Look up the GUID recorded for a Value, if any.
  /// \param V Value whose GUID is requested.
  /// @return The GUID for \p V, or nullopt if none is recorded.
  std::optional<GlobalValue::GUID> getGUID(const Value *V) const {
    const auto It = ValueToGUIDMap.find(V);
    if (It == ValueToGUIDMap.end())
      return std::nullopt;

    return It->getSecond();
  }

  /// Record a GUID for a Value, typically populated from bitcode.
  /// \param V Value to associate with \p GUID.
  /// \param GUID Global unique identifier to store.
  void insertGUID(const Value *V, GlobalValue::GUID GUID) {
    const auto [It, WasInserted] = ValueToGUIDMap.insert({V, GUID});

    (void)It, (void)WasInserted;
#ifndef NDEBUG
    if (!WasInserted) {
      assert((It->second == GUID) && "insertGUID called with different value");
    }
#endif
  }

private:
  /// A mapping directly from Value to GUID. Populated from bitcode
  /// (MODULE_CODE_GUIDLIST). Necessary for lazy-loading modules, where we
  /// don't load metadata.
  DenseMap<const Value *, GlobalValue::GUID> ValueToGUIDMap;

  /// @}
  /// @name Direct access to the globals list, functions list, and symbol table
  /// @{

  /// Get the Module's list of global variables (constant).
  const GlobalListType   &getGlobalList() const       { return GlobalList; }
  /// Get the Module's list of global variables.
  GlobalListType         &getGlobalList()             { return GlobalList; }

  static GlobalListType Module::*getSublistAccess(GlobalVariable*) {
    return &Module::GlobalList;
  }
  friend class llvm::SymbolTableListTraits<llvm::GlobalVariable>;

public:
  /// Get the Module's list of functions (constant).
  /// @return A const reference to the function list.
  const FunctionListType &getFunctionList() const     { return FunctionList; }
  /// Get the Module's list of functions.
  /// @return A mutable reference to the function list.
  FunctionListType       &getFunctionList()           { return FunctionList; }
  /// Return a pointer-to-member for the function list used by list traits.
  /// \param Unused Unused null pointer used to select this overload.
  /// @return A pointer-to-member for Module::FunctionList.
  static FunctionListType Module::*getSublistAccess(Function *Unused) {
    return &Module::FunctionList;
  }

  /// Detach \p Alias from the list but don't delete it.
  /// \param Alias Alias to unlink.
  void removeAlias(GlobalAlias *Alias) { AliasList.remove(Alias); }
  /// Remove \p Alias from the list and delete it.
  /// \param Alias Alias to erase.
  void eraseAlias(GlobalAlias *Alias) { AliasList.erase(Alias); }
  /// Insert \p Alias at the end of the alias list and take ownership.
  /// \param Alias Alias to insert.
  void insertAlias(GlobalAlias *Alias) { AliasList.insert(AliasList.end(), Alias); }
  // Use alias_size() to get the size of AliasList.
  // Use aliases() to get a range of all Alias objects in AliasList.

  /// Detach \p IFunc from the list but don't delete it.
  /// \param IFunc IFunc to unlink.
  void removeIFunc(GlobalIFunc *IFunc) { IFuncList.remove(IFunc); }
  /// Remove \p IFunc from the list and delete it.
  /// \param IFunc IFunc to erase.
  void eraseIFunc(GlobalIFunc *IFunc) { IFuncList.erase(IFunc); }
  /// Insert \p IFunc at the end of the alias list and take ownership.
  /// \param IFunc IFunc to insert.
  void insertIFunc(GlobalIFunc *IFunc) { IFuncList.push_back(IFunc); }
  // Use ifunc_size() to get the number of functions in IFuncList.
  // Use ifuncs() to get the range of all IFuncs.

  /// Detach \p MDNode from the list but don't delete it.
  /// \param MDNode Named metadata node to unlink.
  void removeNamedMDNode(NamedMDNode *MDNode) { NamedMDList.remove(MDNode); }
  /// Remove \p MDNode from the list and delete it.
  /// \param MDNode Named metadata node to erase.
  void eraseNamedMDNode(NamedMDNode *MDNode) { NamedMDList.erase(MDNode); }
  /// Insert \p MDNode at the end of the alias list and take ownership.
  /// \param MDNode Named metadata node to insert.
  void insertNamedMDNode(NamedMDNode *MDNode) {
    NamedMDList.push_back(MDNode);
  }
  // Use named_metadata_size() to get the size of the named meatadata list.
  // Use named_metadata() to get the range of all named metadata.

private: // Please use functions like insertAlias(), removeAlias() etc.
  /// Get the Module's list of aliases (constant).
  const AliasListType    &getAliasList() const        { return AliasList; }
  /// Get the Module's list of aliases.
  AliasListType          &getAliasList()              { return AliasList; }

  static AliasListType Module::*getSublistAccess(GlobalAlias*) {
    return &Module::AliasList;
  }
  friend class llvm::SymbolTableListTraits<llvm::GlobalAlias>;

  /// Get the Module's list of ifuncs (constant).
  const IFuncListType    &getIFuncList() const        { return IFuncList; }
  /// Get the Module's list of ifuncs.
  IFuncListType          &getIFuncList()              { return IFuncList; }

  static IFuncListType Module::*getSublistAccess(GlobalIFunc*) {
    return &Module::IFuncList;
  }
  friend class llvm::SymbolTableListTraits<llvm::GlobalIFunc>;

  /// Get the Module's list of named metadata (constant).
  const NamedMDListType  &getNamedMDList() const      { return NamedMDList; }
  /// Get the Module's list of named metadata.
  NamedMDListType        &getNamedMDList()            { return NamedMDList; }

  static NamedMDListType Module::*getSublistAccess(NamedMDNode*) {
    return &Module::NamedMDList;
  }

public:
  /// Get the symbol table of global variable and function identifiers
  /// @return A const reference to the value symbol table.
  const ValueSymbolTable &getValueSymbolTable() const { return *ValSymTab; }
  /// Get the Module's symbol table of global variable and function identifiers.
  /// @return A mutable reference to the value symbol table.
  ValueSymbolTable       &getValueSymbolTable()       { return *ValSymTab; }

  /// Get the Module's symbol table for COMDATs (constant).
  /// @return A const reference to the COMDAT symbol table.
  const ComdatSymTabType &getComdatSymbolTable() const { return ComdatSymTab; }
  /// Get the Module's symbol table for COMDATs.
  /// @return A mutable reference to the COMDAT symbol table.
  ComdatSymTabType &getComdatSymbolTable() { return ComdatSymTab; }

/// @}
/// @name Global Variable Iteration
/// @{

  /// Return an iterator to the first global variable.
  /// @return A begin iterator for global variables.
  global_iterator       global_begin()       { return GlobalList.begin(); }
  /// Return a const iterator to the first global variable.
  /// @return A const begin iterator for global variables.
  const_global_iterator global_begin() const { return GlobalList.begin(); }
  /// Return an iterator past the last global variable.
  /// @return An end iterator for global variables.
  global_iterator       global_end  ()       { return GlobalList.end(); }
  /// Return a const iterator past the last global variable.
  /// @return A const end iterator for global variables.
  const_global_iterator global_end  () const { return GlobalList.end(); }
  /// Return the number of global variables.
  /// @return The number of global variables.
  size_t                global_size () const { return GlobalList.size(); }
  /// Return true if this module has no global variables.
  /// @return True if there are no global variables.
  bool                  global_empty() const { return GlobalList.empty(); }

  /// Return a range over the global variables.
  /// @return A range over global variables.
  iterator_range<global_iterator> globals() {
    return make_range(global_begin(), global_end());
  }
  /// Return a const range over the global variables.
  /// @return A const range over global variables.
  iterator_range<const_global_iterator> globals() const {
    return make_range(global_begin(), global_end());
  }

/// @}
/// @name Function Iteration
/// @{

  /// Return an iterator to the first function.
  /// @return A begin iterator for functions.
  iterator                begin()       { return FunctionList.begin(); }
  /// Return a const iterator to the first function.
  /// @return A const begin iterator for functions.
  const_iterator          begin() const { return FunctionList.begin(); }
  /// Return an iterator past the last function.
  /// @return An end iterator for functions.
  iterator                end  ()       { return FunctionList.end();   }
  /// Return a const iterator past the last function.
  /// @return A const end iterator for functions.
  const_iterator          end  () const { return FunctionList.end();   }
  /// Return a reverse iterator to the last function.
  /// @return A reverse begin iterator for functions.
  reverse_iterator        rbegin()      { return FunctionList.rbegin(); }
  /// Return a const reverse iterator to the last function.
  /// @return A const reverse begin iterator for functions.
  const_reverse_iterator  rbegin() const{ return FunctionList.rbegin(); }
  /// Return a reverse iterator past the first function.
  /// @return A reverse end iterator for functions.
  reverse_iterator        rend()        { return FunctionList.rend(); }
  /// Return a const reverse iterator past the first function.
  /// @return A const reverse end iterator for functions.
  const_reverse_iterator  rend() const  { return FunctionList.rend(); }
  /// Return the number of functions.
  /// @return The number of functions.
  size_t                  size() const  { return FunctionList.size(); }
  /// Return true if this module has no functions.
  /// @return True if there are no functions.
  bool                    empty() const { return FunctionList.empty(); }

  /// Return a range over the functions.
  /// @return A range over functions.
  iterator_range<iterator> functions() {
    return make_range(begin(), end());
  }
  /// Return a const range over the functions.
  /// @return A const range over functions.
  iterator_range<const_iterator> functions() const {
    return make_range(begin(), end());
  }

  /// Get an iterator range over all function definitions (excluding
  /// declarations).
  /// @return A filtered range of function definitions.
  auto getFunctionDefs() {
    return make_filter_range(functions(),
                             [](Function &F) { return !F.isDeclaration(); });
  }
  /// Get a const iterator range over function definitions, excluding
  /// declarations.
  /// @return A const filtered range of function definitions.
  auto getFunctionDefs() const {
    return make_filter_range(
        functions(), [](const Function &F) { return !F.isDeclaration(); });
  }

/// @}
/// @name Alias Iteration
/// @{

  /// Return an iterator to the first alias.
  /// @return A begin iterator for aliases.
  alias_iterator       alias_begin()            { return AliasList.begin(); }
  /// Return a const iterator to the first alias.
  /// @return A const begin iterator for aliases.
  const_alias_iterator alias_begin() const      { return AliasList.begin(); }
  /// Return an iterator past the last alias.
  /// @return An end iterator for aliases.
  alias_iterator       alias_end  ()            { return AliasList.end();   }
  /// Return a const iterator past the last alias.
  /// @return A const end iterator for aliases.
  const_alias_iterator alias_end  () const      { return AliasList.end();   }
  /// Return the number of aliases.
  /// @return The number of aliases.
  size_t               alias_size () const      { return AliasList.size();  }
  /// Return true if this module has no aliases.
  /// @return True if there are no aliases.
  bool                 alias_empty() const      { return AliasList.empty(); }

  /// Return a range over the aliases.
  /// @return A range over aliases.
  iterator_range<alias_iterator> aliases() {
    return make_range(alias_begin(), alias_end());
  }
  /// Return a const range over the aliases.
  /// @return A const range over aliases.
  iterator_range<const_alias_iterator> aliases() const {
    return make_range(alias_begin(), alias_end());
  }

/// @}
/// @name IFunc Iteration
/// @{

  /// Return an iterator to the first ifunc.
  /// @return A begin iterator for ifuncs.
  ifunc_iterator       ifunc_begin()            { return IFuncList.begin(); }
  /// Return a const iterator to the first ifunc.
  /// @return A const begin iterator for ifuncs.
  const_ifunc_iterator ifunc_begin() const      { return IFuncList.begin(); }
  /// Return an iterator past the last ifunc.
  /// @return An end iterator for ifuncs.
  ifunc_iterator       ifunc_end  ()            { return IFuncList.end();   }
  /// Return a const iterator past the last ifunc.
  /// @return A const end iterator for ifuncs.
  const_ifunc_iterator ifunc_end  () const      { return IFuncList.end();   }
  /// Return the number of ifuncs.
  /// @return The number of ifuncs.
  size_t               ifunc_size () const      { return IFuncList.size();  }
  /// Return true if this module has no ifuncs.
  /// @return True if there are no ifuncs.
  bool                 ifunc_empty() const      { return IFuncList.empty(); }

  /// Return a range over the ifuncs.
  /// @return A range over ifuncs.
  iterator_range<ifunc_iterator> ifuncs() {
    return make_range(ifunc_begin(), ifunc_end());
  }
  /// Return a const range over the ifuncs.
  /// @return A const range over ifuncs.
  iterator_range<const_ifunc_iterator> ifuncs() const {
    return make_range(ifunc_begin(), ifunc_end());
  }

  /// @}
  /// @name Convenience iterators
  /// @{

  /// Iterator over functions and global variables as GlobalObject.
  using global_object_iterator =
      concat_iterator<GlobalObject, iterator, global_iterator>;
  /// Const iterator over functions and global variables as GlobalObject.
  using const_global_object_iterator =
      concat_iterator<const GlobalObject, const_iterator,
                      const_global_iterator>;

  /// Return a range over functions and global variables as GlobalObject.
  /// @return A range over functions and globals as GlobalObject.
  iterator_range<global_object_iterator> global_objects();
  /// Return a const range over functions and global variables as GlobalObject.
  /// @return A const range over functions and globals as GlobalObject.
  iterator_range<const_global_object_iterator> global_objects() const;

  /// Iterator over functions, globals, aliases, and ifuncs as GlobalValue.
  using global_value_iterator =
      concat_iterator<GlobalValue, iterator, global_iterator, alias_iterator,
                      ifunc_iterator>;
  /// Const iterator over functions, globals, aliases, and ifuncs as
  /// GlobalValue.
  using const_global_value_iterator =
      concat_iterator<const GlobalValue, const_iterator, const_global_iterator,
                      const_alias_iterator, const_ifunc_iterator>;

  /// Return a range over all global values in this module.
  /// @return A range over all global values.
  iterator_range<global_value_iterator> global_values();
  /// Return a const range over all global values in this module.
  /// @return A const range over all global values.
  iterator_range<const_global_value_iterator> global_values() const;

  /// @}
  /// @name Named Metadata Iteration
  /// @{

  /// Return an iterator to the first named metadata node.
  /// @return A begin iterator for named metadata.
  named_metadata_iterator named_metadata_begin() { return NamedMDList.begin(); }
  /// Return a const iterator to the first named metadata node.
  /// @return A const begin iterator for named metadata.
  const_named_metadata_iterator named_metadata_begin() const {
    return NamedMDList.begin();
  }

  /// Return an iterator past the last named metadata node.
  /// @return An end iterator for named metadata.
  named_metadata_iterator named_metadata_end() { return NamedMDList.end(); }
  /// Return a const iterator past the last named metadata node.
  /// @return A const end iterator for named metadata.
  const_named_metadata_iterator named_metadata_end() const {
    return NamedMDList.end();
  }

  /// Return the number of named metadata nodes.
  /// @return The number of named metadata nodes.
  size_t named_metadata_size() const { return NamedMDList.size();  }
  /// Return true if this module has no named metadata nodes.
  /// @return True if there are no named metadata nodes.
  bool named_metadata_empty() const { return NamedMDList.empty(); }

  /// Return a range over the named metadata nodes.
  /// @return A range over named metadata nodes.
  iterator_range<named_metadata_iterator> named_metadata() {
    return make_range(named_metadata_begin(), named_metadata_end());
  }
  /// Return a const range over the named metadata nodes.
  /// @return A const range over named metadata nodes.
  iterator_range<const_named_metadata_iterator> named_metadata() const {
    return make_range(named_metadata_begin(), named_metadata_end());
  }

  /// An iterator for DICompileUnits that skips those marked NoDebug.
  class debug_compile_units_iterator {
    NamedMDNode *CUs;
    unsigned Idx;

    LLVM_ABI void SkipNoDebugCUs();

  public:
    /// Iterator category tag for this input iterator.
    using iterator_category = std::input_iterator_tag;
    /// Pointer to a compile unit yielded by this iterator.
    using value_type = DICompileUnit *;
    /// Signed distance between compile-unit iterators.
    using difference_type = std::ptrdiff_t;
    /// Pointer to the yielded compile-unit pointer.
    using pointer = value_type *;
    /// Reference to the yielded compile-unit pointer.
    using reference = value_type &;

    /// Construct an iterator over compile units in \p CUs at index \p Idx.
    /// \param CUs \c llvm.dbg.cu named metadata, or null.
    /// \param Idx Operand index to start at; NoDebug units are skipped.
    explicit debug_compile_units_iterator(NamedMDNode *CUs, unsigned Idx)
        : CUs(CUs), Idx(Idx) {
      SkipNoDebugCUs();
    }

    /// Advance to the next compile unit that is not marked NoDebug.
    /// @return A reference to this iterator after advancement.
    debug_compile_units_iterator &operator++() {
      ++Idx;
      SkipNoDebugCUs();
      return *this;
    }

    /// Advance to the next compile unit and return the previous position.
    /// \param Unused Distinguishes postincrement from preincrement.
    /// @return A copy of the iterator before advancement.
    debug_compile_units_iterator operator++(int Unused) {
      debug_compile_units_iterator T(*this);
      ++Idx;
      return T;
    }

    /// Return true if this iterator is at the same position as \p I.
    /// \param I Iterator to compare against.
    /// @return True if the iterators are at the same position.
    bool operator==(const debug_compile_units_iterator &I) const {
      return Idx == I.Idx;
    }

    /// Return true if this iterator is not at the same position as \p I.
    /// \param I Iterator to compare against.
    /// @return True if the iterators are at different positions.
    bool operator!=(const debug_compile_units_iterator &I) const {
      return Idx != I.Idx;
    }

    /// Return the compile unit at the current position.
    /// @return The DICompileUnit at this iterator position.
    LLVM_ABI DICompileUnit *operator*() const;
    /// Return the compile unit at the current position.
    /// @return The DICompileUnit at this iterator position.
    LLVM_ABI DICompileUnit *operator->() const;
  };

  /// Return an iterator to the first non-NoDebug compile unit.
  /// @return A begin iterator over non-NoDebug compile units.
  debug_compile_units_iterator debug_compile_units_begin() const {
    auto *CUs = getNamedMetadata("llvm.dbg.cu");
    return debug_compile_units_iterator(CUs, 0);
  }

  /// Return an iterator past the last compile unit.
  /// @return An end iterator for the debug compile-unit range.
  debug_compile_units_iterator debug_compile_units_end() const {
    auto *CUs = getNamedMetadata("llvm.dbg.cu");
    return debug_compile_units_iterator(CUs, CUs ? CUs->getNumOperands() : 0);
  }

  /// Return an iterator for all DICompileUnits listed in this Module's
  /// llvm.dbg.cu named metadata node and aren't explicitly marked as
  /// NoDebug.
  /// @return A range over non-NoDebug compile units in this module.
  iterator_range<debug_compile_units_iterator> debug_compile_units() const {
    auto *CUs = getNamedMetadata("llvm.dbg.cu");
    return make_range(
        debug_compile_units_iterator(CUs, 0),
        debug_compile_units_iterator(CUs, CUs ? CUs->getNumOperands() : 0));
  }
/// @}

/// @name Utility functions for printing and dumping Module objects
/// @{

  /// Print the module to an output stream.
  ///
  /// If \c ShouldPreserveUseListOrder, then include uselistorder directives so
  /// that use-lists can be recreated when reading the assembly.
  /// \param OS Stream to print to.
  /// \param AAW Optional annotation writer, or null.
  /// \param ShouldPreserveUseListOrder Emit uselistorder directives when true.
  /// \param IsForDebug Print extra debug-oriented details when true.
  void print(raw_ostream &OS, AssemblyAnnotationWriter *AAW,
             bool ShouldPreserveUseListOrder = false,
             bool IsForDebug = false) const;

  /// Dump the module to stderr (for debugging).
  void dump() const;

  /// Drop all references held by values in this module.
  ///
  /// This function causes all the subinstructions to "let go" of all references
  /// that they are maintaining.  This allows one to 'delete' a whole class at
  /// a time, even though there may be circular references... first all
  /// references are dropped, and all use counts go to zero.  Then everything
  /// is delete'd for real.  Note that no operations are valid on an object
  /// that has "dropped all references", except operator delete.
  void dropAllReferences();

/// @}
/// @name Utility functions for querying Debug information.
/// @{

  /// Returns the Number of Register ParametersDwarf Version by checking
  /// module flags.
  /// @return The number of register parameters from module flags.
  unsigned getNumberRegisterParameters() const;

  /// Returns the Dwarf Version by checking module flags.
  /// @return The DWARF version from module flags.
  unsigned getDwarfVersion() const;

  /// Returns the DWARF format by checking module flags.
  /// @return True if DWARF64 format is enabled.
  bool isDwarf64() const;

  /// Returns the CodeView Version by checking module flags.
  /// Returns zero if not present in module.
  /// @return The CodeView version, or zero if absent.
  unsigned getCodeViewFlag() const;

/// @}
/// @name Utility functions for querying and setting PIC level
/// @{

  /// Returns the PIC level (small or large model)
  /// @return The PIC level from module flags.
  PICLevel::Level getPICLevel() const;

  /// Set the PIC level (small or large model)
  /// \param PL PIC level to record as a module flag.
  void setPICLevel(PICLevel::Level PL);
/// @}

/// @}
/// @name Utility functions for querying and setting PIE level
/// @{

  /// Returns the PIE level (small or large model)
  /// @return The PIE level from module flags.
  PIELevel::Level getPIELevel() const;

  /// Set the PIE level (small or large model)
  /// \param PL PIE level to record as a module flag.
  void setPIELevel(PIELevel::Level PL);
/// @}

  /// @}
  /// @name Utility function for querying and setting code model
  /// @{

  /// Returns the code model (tiny, small, kernel, medium or large model)
  /// @return The code model, or nullopt if unset.
  std::optional<CodeModel::Model> getCodeModel() const;

  /// Set the code model (tiny, small, kernel, medium or large)
  /// \param CL Code model to record as a module flag.
  void setCodeModel(CodeModel::Model CL);
  /// @}

  /// @}
  /// @name Utility functions for querying the long double format
  /// @{

  /// Returns the long double format from the "long-double-type" module flag,
  /// or the triple default when the flag is absent.
  /// @return The long double format for this module.
  LongDoubleFormat getLongDoubleFormat() const;

  /// Set the long double format.
  /// \param Format Long-double format to record as a module flag.
  void setLongDoubleFormat(LongDoubleFormat Format);
  /// @}

  /// @}
  /// @name Utility function for querying the floating-point ABI
  /// @{

  /// Returns the floating-point ABI recorded by the "float-abi" module flag, or
  /// the ABI implied by the target triple when the flag is absent.
  /// @return The floating-point ABI type for this module.
  FloatABI::ABIType getFloatABI() const;
  /// @}

  /// @}
  /// @name Utility function for querying and setting the large data threshold
  /// @{

  /// Returns the large data threshold.
  /// @return The large data threshold, or nullopt if unset.
  std::optional<uint64_t> getLargeDataThreshold() const;

  /// Set the large data threshold.
  /// \param Threshold Maximum object size in bytes for small data.
  void setLargeDataThreshold(uint64_t Threshold);
  /// @}

  /// @name Utility functions for querying and setting PGO summary
  /// @{

  /// Attach profile summary metadata to this module.
  /// \param M Profile summary metadata node to attach.
  /// \param Kind Which summary to store (instrumented, sample, or CS).
  void setProfileSummary(Metadata *M, ProfileSummary::Kind Kind);

  /// Returns profile summary metadata. When IsCS is true, use the context
  /// sensitive profile summary.
  /// \param IsCS True to return the context-sensitive summary.
  /// @return The profile summary metadata node, or null if absent.
  Metadata *getProfileSummary(bool IsCS) const;
  /// @}

  /// Returns whether semantic interposition is to be respected.
  /// @return True if semantic interposition should be respected.
  bool getSemanticInterposition() const;

  /// Set whether semantic interposition is to be respected.
  /// \param SI True to respect semantic interposition.
  void setSemanticInterposition(bool SI);

  /// Returns true if PLT should be avoided for RTLib calls.
  /// @return True if PLT should be avoided for runtime library calls.
  bool getRtLibUseGOT() const;

  /// Set that PLT should be avoid for RTLib calls.
  void setRtLibUseGOT();

  /// Get/set whether referencing global variables can use direct access
  /// relocations on ELF targets.
  /// @return True if direct-access relocations are allowed.
  bool getDirectAccessExternalData() const;
  /// Set whether referencing global variables can use direct access
  /// relocations on ELF targets.
  /// \param Value True to allow direct-access relocations.
  void setDirectAccessExternalData(bool Value);

  /// Get/set whether synthesized functions should get the uwtable attribute.
  /// @return The unwind-table kind from module flags.
  UWTableKind getUwtable() const;
  /// Set whether synthesized functions should get the uwtable attribute.
  /// \param Kind Unwind-table kind to record as a module flag.
  void setUwtable(UWTableKind Kind);

  /// Get/set whether synthesized functions should get the "frame-pointer"
  /// attribute.
  /// @return The frame-pointer kind from module flags.
  FramePointerKind getFramePointer() const;
  /// Set whether synthesized functions should get the "frame-pointer"
  /// attribute.
  /// \param Kind Frame-pointer kind to record as a module flag.
  void setFramePointer(FramePointerKind Kind);

  /// Get/set what kind of stack protector guard to use.
  /// @return The stack protector guard kind string.
  StringRef getStackProtectorGuard() const;
  /// Set what kind of stack protector guard to use.
  /// \param Kind Guard kind, such as "tls", "global", or "sysreg".
  void setStackProtectorGuard(StringRef Kind);

  /// Get/set which register to use as the stack protector guard register. The
  /// empty string is equivalent to "global". Other values may be "tls" or
  /// "sysreg".
  /// @return The guard register name, or empty for the default ("global").
  StringRef getStackProtectorGuardReg() const;
  /// Set which register to use as the stack protector guard register.
  /// \param Reg Register name, or empty for the default ("global").
  void setStackProtectorGuardReg(StringRef Reg);

  /// Get/set a symbol to use as the stack protector guard.
  /// @return The symbol name used as the stack protector guard.
  StringRef getStackProtectorGuardSymbol() const;
  /// Set a symbol to use as the stack protector guard.
  /// \param Symbol Symbol name used as the guard.
  void setStackProtectorGuardSymbol(StringRef Symbol);

  /// Get/set what offset from the stack protector to use.
  /// @return The byte offset from the stack protector guard.
  int getStackProtectorGuardOffset() const;
  /// Set the offset from the stack protector guard.
  /// \param Offset Byte offset from the guard.
  void setStackProtectorGuardOffset(int Offset);

  /// Get/set the width in memory of the stack protector guard value.
  /// @return The guard value width in bits, or nullopt if unset.
  std::optional<unsigned> getStackProtectorGuardValueWidth() const;
  /// Set the width in memory of the stack protector guard value.
  /// \param Width Guard value width in bits.
  void setStackProtectorGuardValueWidth(unsigned Width);

  /// Return whether a __stack_protector_loc section should be emitted.
  /// @return True if a __stack_protector_loc section should be emitted.
  bool hasStackProtectorGuardRecord() const;
  /// Set whether to emit a __stack_protector_loc section.
  /// \param Flag True to emit the section.
  void setStackProtectorGuardRecord(bool Flag);

  /// Get/set the stack alignment overridden from the default.
  /// @return The overridden stack alignment in bytes, or 0 for the default.
  unsigned getOverrideStackAlignment() const;
  /// Override the default stack alignment for this module.
  /// \param Align Stack alignment in bytes, or 0 to use the default.
  void setOverrideStackAlignment(unsigned Align);

  /// Return the maximum TLS alignment in bits from module flags, or 0.
  /// @return The maximum TLS alignment in bits, or 0 if unset.
  unsigned getMaxTLSAlignment() const;

  /// @name Utility functions for querying and setting the build SDK version
  /// @{

  /// Attach a build SDK version metadata to this module.
  /// \param V SDK version to record on this module.
  void setSDKVersion(const VersionTuple &V);

  /// Get the build SDK version metadata.
  ///
  /// An empty version is returned if no such metadata is attached.
  /// @return The build SDK version, or empty if no metadata is attached.
  VersionTuple getSDKVersion() const;
  /// @}

  /// Take ownership of the given memory buffer.
  /// \param MB Memory buffer this module should own.
  void setOwnedMemoryBuffer(std::unique_ptr<MemoryBuffer> MB);

  /// Set the partial sample profile ratio in the profile summary module flag,
  /// if applicable.
  /// \param Index Combined module summary used to compute the ratio.
  void setPartialSampleProfileRatio(const ModuleSummaryIndex &Index);

  /// Get the Darwin target-variant triple for this module.
  ///
  /// The target variant triple is a string describing a variant of the target
  /// host platform. For example, Mac Catalyst can be a variant target triple
  /// for a macOS target.
  /// @returns a string containing the target variant triple.
  StringRef getDarwinTargetVariantTriple() const;

  /// Set the target variant triple which is a string describing a variant of
  /// the target host platform.
  /// \param T Target-variant triple string, such as a Mac Catalyst triple.
  void setDarwinTargetVariantTriple(StringRef T);

  /// Get the target variant version build SDK version metadata.
  ///
  /// An empty version is returned if no such metadata is attached.
  /// @return The Darwin target-variant SDK version, or empty if absent.
  VersionTuple getDarwinTargetVariantSDKVersion() const;

  /// Set the target variant version build SDK version metadata.
  /// \param Version SDK version to record for the Darwin target variant.
  void setDarwinTargetVariantSDKVersion(VersionTuple Version);

  /// Returns target-abi from MDString, null if target-abi is absent.
  /// @return The target-abi string, or empty if the flag is absent.
  StringRef getTargetABIFromMD();

  /// Get how unwind information should be generated for x64 Windows.
  /// @return The Windows x64 EH unwind mode from module flags.
  WinX64EHUnwindMode getWinX64EHUnwindMode() const;

  /// Gets the Control Flow Guard mode.
  /// @return The Control Flow Guard mode from module flags.
  ControlFlowGuardMode getControlFlowGuardMode() const;
};

/// Given "llvm.used" or "llvm.compiler.used" as a global name, collect the
/// initializer elements of that global in a SmallVector and return the global
/// itself.
/// \param M Module that contains the used global.
/// \param Vec Output list of referenced global values.
/// \param CompilerUsed True to read \c llvm.compiler.used, false for
/// \c llvm.used.
/// @return The used global variable, or null if it is absent.
LLVM_ABI GlobalVariable *
collectUsedGlobalVariables(const Module &M, SmallVectorImpl<GlobalValue *> &Vec,
                           bool CompilerUsed);

/// An raw_ostream inserter for modules.
/// \param O Stream to print the module to.
/// \param M Module to print.
/// @return The output stream \p O.
inline raw_ostream &operator<<(raw_ostream &O, const Module &M) {
  M.print(O, nullptr);
  return O;
}

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Convert an opaque \c LLVMModuleRef to a \c Module pointer.
/// \param P Opaque C API module reference to unwrap.
/// @return The Module pointer corresponding to \p P.
inline Module *unwrap(LLVMModuleRef P) {
  return reinterpret_cast<Module *>(P);
}

/// Convert a \c Module pointer to an opaque \c LLVMModuleRef.
/// \param P Module to wrap for the C API.
/// @return An opaque C API module reference for \p P.
inline LLVMModuleRef wrap(const Module *P) {
  return reinterpret_cast<LLVMModuleRef>(const_cast<Module *>(P));
}

/// Unwrap a historical \c LLVMModuleProviderRef as a \c Module pointer.
///
/// LLVMModuleProviderRef exists for historical reasons, but now just holds a
/// Module.
/// \param MP Opaque C API module-provider reference to unwrap.
/// @return The Module pointer held by \p MP.
inline Module *unwrap(LLVMModuleProviderRef MP) {
  return reinterpret_cast<Module*>(MP);
}

} // end namespace llvm

#endif // LLVM_IR_MODULE_H
