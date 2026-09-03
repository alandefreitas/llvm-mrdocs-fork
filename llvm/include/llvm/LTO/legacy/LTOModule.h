//===-LTOModule.h - LLVM Link Time Optimizer ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the LTOModule class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LEGACY_LTOMODULE_H
#define LLVM_LTO_LEGACY_LTOMODULE_H

#include "llvm-c/lto.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Module.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Object/ModuleSymbolTable.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetMachine.h"
#include <string>
#include <vector>

// Forward references to llvm classes.
namespace llvm {
  class Function;
  class GlobalValue;
  class MemoryBuffer;
  class TargetOptions;
  class Value;

//===----------------------------------------------------------------------===//
/// C++ class which implements the opaque lto_module_t type.
///
struct LTOModule {
private:
  struct NameAndAttributes {
    StringRef name;
    uint32_t           attributes = 0;
    bool               isFunction = false;
    const GlobalValue *symbol = nullptr;
  };

  std::unique_ptr<LLVMContext> OwnedContext;

  std::string LinkerOpts;

  std::unique_ptr<Module> Mod;
  MemoryBufferRef MBRef;
  ModuleSymbolTable SymTab;
  std::unique_ptr<TargetMachine> _target;
  std::vector<NameAndAttributes> _symbols;

  // _defines and _undefines only needed to disambiguate tentative definitions
  StringSet<>                             _defines;
  StringMap<NameAndAttributes> _undefines;
  std::vector<StringRef> _asm_undefines;

  LTOModule(std::unique_ptr<Module> M, MemoryBufferRef MBRef,
            TargetMachine *TM);

public:
  /// Destroy this LTO module.
  LLVM_ABI ~LTOModule();

  /// Returns 'true' if the file or memory contents is LLVM bitcode.
  /// @param mem Pointer to the memory contents to inspect.
  /// @param length Number of bytes at \p mem.
  /// @return True if the memory contents are LLVM bitcode.
  LLVM_ABI static bool isBitcodeFile(const void *mem, size_t length);
  /// Returns 'true' if the file at \p path contains LLVM bitcode.
  /// @param path Path of the file to inspect.
  /// @return True if the file contains LLVM bitcode.
  LLVM_ABI static bool isBitcodeFile(StringRef path);

  /// Returns 'true' if the Module is produced for ThinLTO.
  /// @return True if the Module is produced for ThinLTO.
  LLVM_ABI bool isThinLTO();

  /// Returns 'true' if the memory buffer is LLVM bitcode for the specified
  /// triple.
  /// @param memBuffer Memory buffer that may contain bitcode.
  /// @param triplePrefix Target triple prefix that the bitcode must match.
  /// @return True if the buffer is LLVM bitcode for the specified triple.
  LLVM_ABI static bool isBitcodeForTarget(MemoryBuffer *memBuffer,
                                          StringRef triplePrefix);

  /// Returns a string representing the producer identification stored in the
  /// bitcode, or "" if the bitcode does not contains any.
  /// @param Buffer Memory buffer that may contain bitcode.
  /// @return Producer identification string, or "" if none.
  LLVM_ABI static std::string getProducerString(MemoryBuffer *Buffer);

  /// Create a MemoryBuffer from a memory range with an optional name.
  /// @param mem Pointer to the first byte of the range.
  /// @param length Number of bytes in the range.
  /// @param name Optional buffer name used for diagnostics.
  /// @return MemoryBuffer covering the given memory range.
  LLVM_ABI static std::unique_ptr<MemoryBuffer>
  makeBuffer(const void *mem, size_t length, StringRef name = "");

  /// Create an LTOModule from a bitcode file path.
  ///
  /// N.B. These methods take ownership of the buffer. The caller must have
  /// initialized the Targets, the TargetMCs, the AsmPrinters, and the
  /// AsmParsers by calling:
  ///
  /// InitializeAllTargets();
  /// InitializeAllTargetMCs();
  /// InitializeAllAsmPrinters();
  /// InitializeAllAsmParsers();
  /// @param Context LLVM context used to parse the module.
  /// @param path Path of the bitcode file to load.
  /// @param options Target options used to create the target machine.
  /// @return Owned LTOModule, or an error.
  LLVM_ABI static ErrorOr<std::unique_ptr<LTOModule>>
  createFromFile(LLVMContext &Context, StringRef path,
                 const TargetOptions &options);
  /// Create an LTOModule from an open file descriptor.
  /// @param Context LLVM context used to parse the module.
  /// @param fd Open file descriptor to map.
  /// @param path Path used as the buffer name for diagnostics.
  /// @param size Number of bytes to map from the start of the file.
  /// @param options Target options used to create the target machine.
  /// @return Owned LTOModule, or an error.
  LLVM_ABI static ErrorOr<std::unique_ptr<LTOModule>>
  createFromOpenFile(LLVMContext &Context, int fd, StringRef path, size_t size,
                     const TargetOptions &options);
  /// Create an LTOModule from a slice of an open file.
  /// @param Context LLVM context used to parse the module.
  /// @param fd Open file descriptor to map.
  /// @param path Path used as the buffer name for diagnostics.
  /// @param map_size Number of bytes to map.
  /// @param offset Byte offset within the file where the slice begins.
  /// @param options Target options used to create the target machine.
  /// @return Owned LTOModule, or an error.
  LLVM_ABI static ErrorOr<std::unique_ptr<LTOModule>>
  createFromOpenFileSlice(LLVMContext &Context, int fd, StringRef path,
                          size_t map_size, off_t offset,
                          const TargetOptions &options);
  /// Create an LTOModule from an in-memory bitcode buffer.
  /// @param Context LLVM context used to parse the module.
  /// @param mem Pointer to the bitcode bytes.
  /// @param length Number of bytes at \p mem.
  /// @param options Target options used to create the target machine.
  /// @param path Optional path used as the buffer name for diagnostics.
  /// @return Owned LTOModule, or an error.
  LLVM_ABI static ErrorOr<std::unique_ptr<LTOModule>>
  createFromBuffer(LLVMContext &Context, const void *mem, size_t length,
                   const TargetOptions &options, StringRef path = "");
  /// Create an LTOModule that owns its own LLVMContext.
  /// @param Context LLVM context transferred into the module.
  /// @param mem Pointer to the bitcode bytes.
  /// @param length Number of bytes at \p mem.
  /// @param options Target options used to create the target machine.
  /// @param path Path used as the buffer name for diagnostics.
  /// @return Owned LTOModule, or an error.
  LLVM_ABI static ErrorOr<std::unique_ptr<LTOModule>>
  createInLocalContext(std::unique_ptr<LLVMContext> Context, const void *mem,
                       size_t length, const TargetOptions &options,
                       StringRef path);

  /// Return a const reference to the wrapped Module.
  /// @return Const reference to the wrapped Module.
  const Module &getModule() const { return *Mod; }
  /// Return a mutable reference to the wrapped Module.
  /// @return Mutable reference to the wrapped Module.
  Module &getModule() { return *Mod; }

  /// Take ownership of the wrapped Module.
  /// @return Unique pointer that owns the wrapped Module.
  std::unique_ptr<Module> takeModule() { return std::move(Mod); }

  /// Return the Module's target triple.
  /// @return The Module's target triple.
  const Triple &getTargetTriple() { return getModule().getTargetTriple(); }

  /// Set the Module's target triple.
  /// @param T Target triple to assign to the module.
  void setTargetTriple(Triple T) { getModule().setTargetTriple(T); }

  /// Get the number of symbols
  /// @return Number of symbols in the module.
  uint32_t getSymbolCount() {
    return _symbols.size();
  }

  /// Get the attributes for a symbol at the specified index.
  /// @param index Zero-based index into the module's symbol table.
  /// @return Symbol attributes, or zero if the index is out of range.
  lto_symbol_attributes getSymbolAttributes(uint32_t index) {
    if (index < _symbols.size())
      return lto_symbol_attributes(_symbols[index].attributes);
    return lto_symbol_attributes(0);
  }

  /// Get the name of the symbol at the specified index.
  /// @param index Zero-based index into the module's symbol table.
  /// @return Symbol name, or an empty StringRef if the index is out of range.
  StringRef getSymbolName(uint32_t index) {
    if (index < _symbols.size())
      return _symbols[index].name;
    return StringRef();
  }

  /// Get the number of undefined symbols from module-level assembly.
  /// @return Number of undefined symbols from module-level assembly.
  uint32_t getAsmUndefSymbolCount() { return _asm_undefines.size(); }

  /// Get the name of an undefined asm symbol at the specified index.
  /// @param index Zero-based index into the asm-undefined symbol list.
  /// @return Symbol name, or an empty StringRef if the index is out of range.
  StringRef getAsmUndefSymbolName(uint32_t index) {
    if (index < _asm_undefines.size())
      return _asm_undefines[index];
    return StringRef();
  }

  /// Get the GlobalValue for a symbol at the specified index.
  /// @param index Zero-based index into the module's symbol table.
  /// @return GlobalValue for the symbol, or nullptr if the index is out of range.
  const GlobalValue *getSymbolGV(uint32_t index) {
    if (index < _symbols.size())
      return _symbols[index].symbol;
    return nullptr;
  }

  /// Return linker options extracted from the module metadata.
  /// @return Linker options extracted from the module metadata.
  StringRef getLinkerOpts() { return LinkerOpts; }

  /// Return the list of undefined symbols from module-level assembly.
  /// @return List of undefined symbols from module-level assembly.
  const std::vector<StringRef> &getAsmUndefinedRefs() { return _asm_undefines; }

  /// Create an LTO input file from an in-memory object or bitcode buffer.
  /// @param buffer Pointer to the object or bitcode bytes.
  /// @param buffer_size Number of bytes at \p buffer.
  /// @param path Path used as the buffer name for diagnostics.
  /// @param out_error Destination for an error message on failure.
  /// @return LTO input file, or nullptr on failure.
  LLVM_ABI static lto::InputFile *createInputFile(const void *buffer,
                                                  size_t buffer_size,
                                                  const char *path,
                                                  std::string &out_error);

  /// Get the number of dependent libraries recorded in an input file.
  /// @param input LTO input file whose dependent libraries are counted.
  /// @return Number of dependent libraries recorded in the input file.
  LLVM_ABI static size_t getDependentLibraryCount(lto::InputFile *input);

  /// Get a dependent library name from an input file.
  /// @param input LTO input file that owns the dependent library list.
  /// @param index Zero-based index into the dependent library list.
  /// @param size Destination for the length of the returned name.
  /// @return Dependent library name, with length stored in \p size.
  LLVM_ABI static const char *getDependentLibrary(lto::InputFile *input,
                                                  size_t index, size_t *size);

  /// Return the Mach-O CPU type for the module's target triple.
  /// @return Mach-O CPU type, or an error.
  LLVM_ABI Expected<uint32_t> getMachOCPUType() const;

  /// Return the Mach-O CPU subtype for the module's target triple.
  /// @return Mach-O CPU subtype, or an error.
  LLVM_ABI Expected<uint32_t> getMachOCPUSubType() const;

  /// Returns true if the module has either the @llvm.global_ctors or the
  /// @llvm.global_dtors symbol. Otherwise returns false.
  /// @return True if the module has global constructors or destructors.
  LLVM_ABI bool hasCtorDtor() const;

private:
  /// Parse metadata from the module
  // FIXME: it only parses "llvm.linker.options" metadata at the moment
  // FIXME: can't access metadata in lazily loaded modules
  void parseMetadata();

  /// Parse the symbols from the module and model-level ASM and add them to
  /// either the defined or undefined lists.
  void parseSymbols();

  /// Add a symbol which isn't defined just yet to a list to be resolved later.
  void addPotentialUndefinedSymbol(ModuleSymbolTable::Symbol Sym,
                                   bool isFunc);

  /// Add a defined symbol to the list.
  void addDefinedSymbol(StringRef Name, const GlobalValue *def,
                        bool isFunction);

  /// Add a data symbol as defined to the list.
  void addDefinedDataSymbol(ModuleSymbolTable::Symbol Sym);
  void addDefinedDataSymbol(StringRef Name, const GlobalValue *v);

  /// Add a function symbol as defined to the list.
  void addDefinedFunctionSymbol(ModuleSymbolTable::Symbol Sym);
  void addDefinedFunctionSymbol(StringRef Name, const GlobalValue *F);

  /// Add a global symbol from module-level ASM to the defined list.
  void addAsmGlobalSymbol(StringRef, lto_symbol_attributes scope);

  /// Add a global symbol from module-level ASM to the undefined list.
  void addAsmGlobalSymbolUndef(StringRef);

  /// Parse i386/ppc ObjC class data structure.
  void addObjCClass(const GlobalVariable *clgv);

  /// Parse i386/ppc ObjC category data structure.
  void addObjCCategory(const GlobalVariable *clgv);

  /// Parse i386/ppc ObjC class list data structure.
  void addObjCClassRef(const GlobalVariable *clgv);

  /// Get string that the data pointer points to.
  bool objcClassNameFromExpression(const Constant *c, std::string &name);

  /// Create an LTOModule (private version).
  static ErrorOr<std::unique_ptr<LTOModule>>
  makeLTOModule(MemoryBufferRef Buffer, const TargetOptions &options,
                LLVMContext &Context, bool ShouldBeLazy);
};
}
#endif
