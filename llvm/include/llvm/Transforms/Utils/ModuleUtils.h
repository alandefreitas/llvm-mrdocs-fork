//===-- ModuleUtils.h - Functions to manipulate Modules ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform manipulations on Modules.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_MODULEUTILS_H
#define LLVM_TRANSFORMS_UTILS_MODULEUTILS_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <utility> // for std::pair

namespace llvm {
template <typename T> class SmallVectorImpl;

template <typename T> class ArrayRef;
class Module;
class Function;
class FunctionCallee;
class GlobalIFunc;
class GlobalValue;
class Constant;
class ConstantStruct;
class Value;
class Type;

/// Append \p F to the global constructors of module \p M.
///
/// This wraps the function in the appropriate structure and stores it along
/// side other global constructors. For details see
/// https://llvm.org/docs/LangRef.html#the-llvm-global-ctors-global-variable
///
/// \param M Module whose global constructors are updated.
/// \param F Function to append as a global constructor.
/// \param Priority Priority of the constructor entry.
/// \param Data Optional associated data constant, or nullptr.
LLVM_ABI void appendToGlobalCtors(Module &M, Function *F, int Priority,
                                  Constant *Data = nullptr);

/// Append \p F to the global destructors of module \p M.
///
/// Same as appendToGlobalCtors(), but for global dtors.
///
/// \param M Module whose global destructors are updated.
/// \param F Function to append as a global destructor.
/// \param Priority Priority of the destructor entry.
/// \param Data Optional associated data constant, or nullptr.
LLVM_ABI void appendToGlobalDtors(Module &M, Function *F, int Priority,
                                  Constant *Data = nullptr);

/// Callback that transforms a single global constructor record.
///
/// Receives a constructor constant and returns a replacement, or nullptr to
/// remove that entry.
using GlobalCtorTransformFn = llvm::function_ref<Constant *(Constant *)>;

/// Apply \p Fn to the list of global constructors of module \p M.
///
/// Replaces each constructor record with the one returned by \p Fn. If
/// nullptr was returned, the corresponding constructor will be removed from the
/// array. For details see
/// https://llvm.org/docs/LangRef.html#the-llvm-global-ctors-global-variable
///
/// \param M Module whose global constructors are transformed.
/// \param Fn Transform applied to each constructor record.
LLVM_ABI void transformGlobalCtors(Module &M, const GlobalCtorTransformFn &Fn);

/// Apply \p Fn to the list of global destructors of module \p M.
///
/// Same as transformGlobalCtors(), but for global dtors.
///
/// \param M Module whose global destructors are transformed.
/// \param Fn Transform applied to each destructor record.
LLVM_ABI void transformGlobalDtors(Module &M, const GlobalCtorTransformFn &Fn);

/// Sets the KCFI type for the function.
///
/// Used for compiler-generated functions that are indirectly called in
/// instrumented code.
///
/// \param M Module containing \p F.
/// \param F Function whose KCFI type is set.
/// \param MangledType Mangled KCFI type string to attach.
LLVM_ABI void setKCFIType(Module &M, Function &F, StringRef MangledType);

/// Declare a sanitizer initialization function in module \p M.
///
/// \param M Module in which to declare the init function.
/// \param InitName Name of the sanitizer init function.
/// \param InitArgTypes Argument types of the init function.
/// \param Weak If true, declare the init function as a weak external symbol.
/// \return Callee for the declared sanitizer init function.
LLVM_ABI FunctionCallee
declareSanitizerInitFunction(Module &M, StringRef InitName,
                             ArrayRef<Type *> InitArgTypes, bool Weak = false);

/// Creates sanitizer constructor function.
///
/// \param M Module in which to create the constructor.
/// \param CtorName Name of the constructor function to create.
/// \return Returns pointer to constructor.
LLVM_ABI Function *createSanitizerCtor(Module &M, StringRef CtorName);

/// Creates sanitizer constructor function, and calls sanitizer's init
/// function from it.
///
/// \param M Module in which to create the constructor and init functions.
/// \param CtorName Name of the constructor function to create.
/// \param InitName Name of the sanitizer init function to call.
/// \param InitArgTypes Argument types of the init function.
/// \param InitArgs Argument values passed to the init function.
/// \param VersionCheckName Optional name of a version-check function to call.
/// \param Weak If true, declare the init function as a weak external symbol.
/// \return Returns pair of pointers to constructor, and init functions
/// respectively.
LLVM_ABI std::pair<Function *, FunctionCallee>
createSanitizerCtorAndInitFunctions(Module &M, StringRef CtorName,
                                    StringRef InitName,
                                    ArrayRef<Type *> InitArgTypes,
                                    ArrayRef<Value *> InitArgs,
                                    StringRef VersionCheckName = StringRef(),
                                    bool Weak = false);

/// Creates sanitizer constructor and init functions lazily.
///
/// If a constructor and init function already exist, this function returns
/// them. Otherwise it calls \c createSanitizerCtorAndInitFunctions. The
/// FunctionsCreatedCallback is invoked in that case, passing the new Ctor and
/// Init function.
///
/// \param M Module in which to look up or create the functions.
/// \param CtorName Name of the constructor function.
/// \param InitName Name of the sanitizer init function.
/// \param InitArgTypes Argument types of the init function.
/// \param InitArgs Argument values passed to the init function.
/// \param FunctionsCreatedCallback Invoked when new Ctor and Init are created.
/// \param VersionCheckName Optional name of a version-check function to call.
/// \param Weak If true, declare the init function as a weak external symbol.
/// \return Returns pair of pointers to constructor, and init functions
/// respectively.
LLVM_ABI std::pair<Function *, FunctionCallee>
getOrCreateSanitizerCtorAndInitFunctions(
    Module &M, StringRef CtorName, StringRef InitName,
    ArrayRef<Type *> InitArgTypes, ArrayRef<Value *> InitArgs,
    function_ref<void(Function *, FunctionCallee)> FunctionsCreatedCallback,
    StringRef VersionCheckName = StringRef(), bool Weak = false);

/// Rename all the anon globals in the module using a hash computed from
/// the list of public globals in the module.
///
/// \param M Module whose anonymous globals are renamed.
/// \return True if any anonymous globals were renamed.
LLVM_ABI bool nameUnamedGlobals(Module &M);

/// Adds global values to the llvm.used list.
///
/// \param M Module whose llvm.used list is updated.
/// \param Values Global values to append to llvm.used.
LLVM_ABI void appendToUsed(Module &M, ArrayRef<GlobalValue *> Values);

/// Adds global values to the llvm.compiler.used list.
///
/// \param M Module whose llvm.compiler.used list is updated.
/// \param Values Global values to append to llvm.compiler.used.
LLVM_ABI void appendToCompilerUsed(Module &M, ArrayRef<GlobalValue *> Values);

/// Removes selected entries from llvm.used and llvm.compiler.used.
///
/// \p ShouldRemove should return true for any initializer field that should not
/// be included in the replacement global.
///
/// \param M Module whose used lists are updated.
/// \param ShouldRemove Predicate that returns true for entries to remove.
LLVM_ABI void removeFromUsedLists(Module &M,
                                  function_ref<bool(Constant *)> ShouldRemove);

/// Filter out potentially dead comdat functions where other entries keep the
/// entire comdat group alive.
///
/// This is designed for cases where functions appear to become dead but remain
/// alive due to other live entries in their comdat group.
///
/// The \p DeadComdatFunctions container should only have pointers to
/// `Function`s which are members of a comdat group and are believed to be
/// dead.
///
/// After this routine finishes, the only remaining `Function`s in \p
/// DeadComdatFunctions are those where every member of the comdat is listed
/// and thus removing them is safe (provided *all* are removed).
///
/// \param DeadComdatFunctions Candidate dead comdat functions; filtered in
/// place.
LLVM_ABI void
filterDeadComdatFunctions(SmallVectorImpl<Function *> &DeadComdatFunctions);

/// Produce a unique identifier for this module by taking the MD5 sum of
/// the names of the module's strong external symbols that are not comdat
/// members.
///
/// This identifier is normally guaranteed to be unique, or the program would
/// fail to link due to multiply defined symbols.
///
/// If the module has no strong external symbols (such a module may still have a
/// semantic effect if it performs global initialization), we cannot produce a
/// unique identifier for this module, so we return the empty string.
///
/// \param M Module for which to compute a unique identifier.
/// \return Unique module identifier string, or the empty string if none can be
/// produced.
LLVM_ABI std::string getUniqueModuleId(Module *M);

/// Embed memory buffer \p Buf into module \p M as a global.
///
/// Also provide a metadata entry to identify it in the module using the same
/// section name. If \p SectionExclude is true !exclude is applied to the global
/// in order to apply necessary linkage flags to exclude the section from a
/// link. If false, apply !metadata_section_kind which results in no additional
/// section linkage flags.
///
/// \param M Module into which the buffer is embedded.
/// \param Buf Memory buffer contents to embed as a global.
/// \param SectionName Section name for the global and its metadata entry.
/// \param Alignment Alignment of the embedded global.
/// \param SectionExclude If true, mark the section as excluded from linking.
/// \return Pointer to the global variable that embeds the buffer.
LLVM_ABI GlobalVariable *embedBufferInModule(Module &M, MemoryBufferRef Buf,
                                             StringRef SectionName,
                                             Align Alignment = Align(1),
                                             bool SectionExclude = true);

/// Lower ifunc calls via a global table initialized by a constructor.
///
/// Replaces uses with indirect calls loaded out of a global table initialized
/// in a global constructor. This will introduce one constructor function and
/// adds it to llvm.global_ctors. The constructor will call the resolver
/// function once for each ifunc.
///
/// Leaves any unhandled constant initializer uses as-is.
///
/// If \p IFuncsToLower is empty, all ifuncs in the module will be lowered.
/// If \p IFuncsToLower is non-empty, only the selected ifuncs will be lowered.
///
/// The processed ifuncs without remaining users will be removed from the
/// module.
///
/// \param M Module whose ifunc users are lowered.
/// \param IFuncsToLower IFuncs to lower, or empty to lower all ifuncs in \p M.
/// \return True if any ifunc users could not be lowered and were left as-is.
LLVM_ABI bool
lowerGlobalIFuncUsersAsGlobalCtor(Module &M,
                                  ArrayRef<GlobalIFunc *> IFuncsToLower = {});

} // End llvm namespace

#endif // LLVM_TRANSFORMS_UTILS_MODULEUTILS_H
