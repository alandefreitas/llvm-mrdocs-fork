//===--- yaml2obj.h - -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Common declarations for yaml2obj
//===----------------------------------------------------------------------===//
#ifndef LLVM_OBJECTYAML_YAML2OBJ_H
#define LLVM_OBJECTYAML_YAML2OBJ_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {
class raw_ostream;
template <typename T> class SmallVectorImpl;
class StringRef;
class Twine;

namespace object {
class ObjectFile;
}

namespace COFFYAML {
struct Object;
}

namespace ELFYAML {
struct Object;
}

namespace GOFFYAML {
struct Object;
}

namespace MinidumpYAML {
struct Object;
}

namespace OffloadYAML {
struct Binary;
}

namespace WasmYAML {
struct Object;
}

namespace XCOFFYAML {
struct Object;
}

namespace ArchYAML {
struct Archive;
}

namespace DXContainerYAML {
struct Object;
} // namespace DXContainerYAML

namespace yaml {
class Input;
struct YamlObjectFile;

/// Callback invoked with a diagnostic message when YAML-to-object conversion fails.
using ErrorHandler = llvm::function_ref<void(const Twine &Msg)>;

/// Write the archive described by \p Doc as a binary object to \p Out.
/// \param Doc Parsed YAML description of an archive.
/// \param Out Output stream that receives the binary object.
/// \param EH Handler invoked with a message if conversion fails.
/// \return True on success, false if an error was reported via \p EH.
LLVM_ABI bool yaml2archive(ArchYAML::Archive &Doc, raw_ostream &Out,
                           ErrorHandler EH);
/// Write the COFF object described by \p Doc as a binary object to \p Out.
/// \param Doc Parsed YAML description of a COFF object.
/// \param Out Output stream that receives the binary object.
/// \param EH Handler invoked with a message if conversion fails.
/// \param MaxSize Upper bound on the size of the generated object.
/// \return True on success, false if an error was reported via \p EH.
LLVM_ABI bool yaml2coff(COFFYAML::Object &Doc, raw_ostream &Out,
                        ErrorHandler EH, uint64_t MaxSize);
/// Write the GOFF object described by \p Doc as a binary object to \p Out.
/// \param Doc Parsed YAML description of a GOFF object.
/// \param Out Output stream that receives the binary object.
/// \param EH Handler invoked with a message if conversion fails.
/// \return True on success, false if an error was reported via \p EH.
LLVM_ABI bool yaml2goff(GOFFYAML::Object &Doc, raw_ostream &Out,
                        ErrorHandler EH);
/// Write the ELF object described by \p Doc as a binary object to \p Out.
/// \param Doc Parsed YAML description of an ELF object.
/// \param Out Output stream that receives the binary object.
/// \param EH Handler invoked with a message if conversion fails.
/// \param MaxSize Upper bound on the size of the generated object.
/// \return True on success, false if an error was reported via \p EH.
LLVM_ABI bool yaml2elf(ELFYAML::Object &Doc, raw_ostream &Out, ErrorHandler EH,
                       uint64_t MaxSize);
/// Write the Mach-O object described by \p Doc as a binary object to \p Out.
/// \param Doc Parsed YAML description of a Mach-O or universal Mach-O object.
/// \param Out Output stream that receives the binary object.
/// \param EH Handler invoked with a message if conversion fails.
/// \return True on success, false if an error was reported via \p EH.
LLVM_ABI bool yaml2macho(YamlObjectFile &Doc, raw_ostream &Out,
                         ErrorHandler EH);
/// Write the minidump described by \p Doc as a binary object to \p Out.
/// \param Doc Parsed YAML description of a minidump.
/// \param Out Output stream that receives the binary object.
/// \param EH Handler invoked with a message if conversion fails.
/// \return True on success, false if an error was reported via \p EH.
LLVM_ABI bool yaml2minidump(MinidumpYAML::Object &Doc, raw_ostream &Out,
                            ErrorHandler EH);
/// Write the offload binary described by \p Doc as a binary object to \p Out.
/// \param Doc Parsed YAML description of an offload binary.
/// \param Out Output stream that receives the binary object.
/// \param EH Handler invoked with a message if conversion fails.
/// \return True on success, false if an error was reported via \p EH.
LLVM_ABI bool yaml2offload(OffloadYAML::Binary &Doc, raw_ostream &Out,
                           ErrorHandler EH);
/// Write the Wasm object described by \p Doc as a binary object to \p Out.
/// \param Doc Parsed YAML description of a Wasm object.
/// \param Out Output stream that receives the binary object.
/// \param EH Handler invoked with a message if conversion fails.
/// \return True on success, false if an error was reported via \p EH.
LLVM_ABI bool yaml2wasm(WasmYAML::Object &Doc, raw_ostream &Out,
                        ErrorHandler EH);
/// Write the XCOFF object described by \p Doc as a binary object to \p Out.
/// \param Doc Parsed YAML description of an XCOFF object.
/// \param Out Output stream that receives the binary object.
/// \param EH Handler invoked with a message if conversion fails.
/// \return True on success, false if an error was reported via \p EH.
LLVM_ABI bool yaml2xcoff(XCOFFYAML::Object &Doc, raw_ostream &Out,
                         ErrorHandler EH);
/// Write the DXContainer object described by \p Doc as a binary object to \p Out.
/// \param Doc Parsed YAML description of a DXContainer object.
/// \param Out Output stream that receives the binary object.
/// \param EH Handler invoked with a message if conversion fails.
/// \return True on success, false if an error was reported via \p EH.
LLVM_ABI bool yaml2dxcontainer(DXContainerYAML::Object &Doc, raw_ostream &Out,
                               ErrorHandler EH);

/// Convert the YAML document at \p DocNum from \p YIn into a binary object on \p Out.
/// \param YIn YAML input that may contain one or more documents.
/// \param Out Output stream that receives the binary object.
/// \param ErrHandler Handler invoked with a message if conversion fails.
/// \param DocNum One-based index of the YAML document to convert (default 1).
/// \param MaxSize Upper bound on the size of formats that honor a size limit.
/// \return True on success, false if an error was reported via \p ErrHandler.
LLVM_ABI bool convertYAML(Input &YIn, raw_ostream &Out, ErrorHandler ErrHandler,
                          unsigned DocNum = 1, uint64_t MaxSize = UINT64_MAX);

/// Convenience function for tests.
/// \param Storage Buffer that receives the generated object bytes and owns them.
/// \param Yaml YAML text describing an object file.
/// \param ErrHandler Handler invoked with a message if conversion fails.
/// \return An ObjectFile wrapping \p Storage, or nullptr on failure.
LLVM_ABI std::unique_ptr<object::ObjectFile>
yaml2ObjectFile(SmallVectorImpl<char> &Storage, StringRef Yaml,
                ErrorHandler ErrHandler);

} // namespace yaml
} // namespace llvm

#endif
