//===- IRObjectFile.h - LLVM IR object file implementation ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the IRObjectFile template class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_IROBJECTFILE_H
#define LLVM_OBJECT_IROBJECTFILE_H

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Object/IRSymtab.h"
#include "llvm/Object/ModuleSymbolTable.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Module;

namespace object {
class ObjectFile;

/// SymbolicFile implementation for LLVM IR bitcode modules.
class LLVM_ABI IRObjectFile : public SymbolicFile {
  std::vector<std::unique_ptr<Module>> Mods;
  ModuleSymbolTable SymTab;
  IRObjectFile(MemoryBufferRef Object,
               std::vector<std::unique_ptr<Module>> Mods);

public:
  /// Destroys the IRObjectFile and its owned modules.
  ~IRObjectFile() override;
  /// Advances \p Symb to the next symbol.
  ///
  /// \param Symb Symbol data reference to advance.
  void moveSymbolNext(DataRefImpl &Symb) const override;
  /// Print the name of symbol \p Symb to \p OS.
  ///
  /// \param OS Stream to write the symbol name to.
  /// \param Symb Symbol data reference whose name is printed.
  /// \return Error::success() on success, or an error if printing fails.
  Error printSymbolName(raw_ostream &OS, DataRefImpl Symb) const override;
  /// Flags for symbol \p Symb (bitwise OR of BasicSymbolRef::Flags).
  ///
  /// \param Symb Symbol data reference whose flags are returned.
  /// \return The symbol flags, or an error if unavailable.
  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override;
  /// Iterator to the first symbol in this file.
  ///
  /// \return An iterator to the first symbol in this file.
  basic_symbol_iterator symbol_begin() const override;
  /// Past-the-end iterator for symbols in this file.
  ///
  /// \return A past-the-end iterator for symbols in this file.
  basic_symbol_iterator symbol_end() const override;
  /// True if this file uses a 64-bit address size.
  ///
  /// \return True if this file uses a 64-bit address size.
  bool is64Bit() const override {
    return Triple(getTargetTriple()).isArch64Bit();
  }
  /// Target triple string for the IR modules in this file.
  ///
  /// \return The target triple string for the IR modules in this file.
  StringRef getTargetTriple() const;

  /// True if \p v is an IRObjectFile.
  ///
  /// \param v Binary to test.
  /// \return True if \p v is an IRObjectFile.
  static bool classof(const Binary *v) {
    return v->isIR();
  }

  /// Iterator over the const Module objects contained in this file.
  using module_iterator =
      pointee_iterator<std::vector<std::unique_ptr<Module>>::const_iterator,
                       const Module>;

  /// Iterator to the first module in this file.
  ///
  /// \return An iterator to the first module in this file.
  module_iterator module_begin() const { return module_iterator(Mods.begin()); }
  /// Past-the-end iterator for modules in this file.
  ///
  /// \return A past-the-end iterator for modules in this file.
  module_iterator module_end() const { return module_iterator(Mods.end()); }

  /// Range over all modules in this file.
  ///
  /// \return An iterator range over all modules in this file.
  iterator_range<module_iterator> modules() const {
    return make_range(module_begin(), module_end());
  }

  /// Finds and returns bitcode embedded in the given object file, or an
  /// error code if not found.
  ///
  /// \param Obj Object file that may contain embedded bitcode.
  /// \return A memory buffer reference to the embedded bitcode, or an error
  ///         if not found.
  static Expected<MemoryBufferRef> findBitcodeInObject(const ObjectFile &Obj);

  /// Finds and returns bitcode from a memory buffer.
  ///
  /// The buffer may be either a bitcode file or a native object file with
  /// embedded bitcode. Returns an error code if not found.
  ///
  /// \param Object Memory buffer that may contain bitcode.
  /// \return A memory buffer reference to the bitcode, or an error if not
  ///         found.
  static Expected<MemoryBufferRef>
  findBitcodeInMemBuffer(MemoryBufferRef Object);

  /// Create an IRObjectFile from \p Object using \p Context.
  ///
  /// \param Object Memory buffer holding bitcode or an object with embedded
  ///        bitcode.
  /// \param Context LLVM context used to parse the modules.
  /// \return A unique pointer to the created IRObjectFile, or an error on
  ///         failure.
  static Expected<std::unique_ptr<IRObjectFile>> create(MemoryBufferRef Object,
                                                        LLVMContext &Context);
};

/// The contents of a bitcode file and its irsymtab. Any underlying data
/// for the irsymtab are owned by Symtab and Strtab.
struct IRSymtabFile {
  /// Bitcode modules extracted from the file.
  std::vector<BitcodeModule> Mods;
  SmallVector<char, 0> Symtab, ///< Serialized irsymtab contents.
      Strtab;                  ///< String table backing the irsymtab.
  /// Reader over the irsymtab stored in Symtab and Strtab.
  irsymtab::Reader TheReader;
};

/// Reads a bitcode file, creating its irsymtab if necessary.
///
/// \param MBRef Memory buffer holding the bitcode file.
/// \return An IRSymtabFile for the bitcode, or an error on failure.
LLVM_ABI Expected<IRSymtabFile> readIRSymtab(MemoryBufferRef MBRef);
}

} // namespace llvm

#endif
