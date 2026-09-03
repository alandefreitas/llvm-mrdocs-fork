//===-- LVReaderHandler.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements the Reader handler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVREADERHANDLER_H
#define LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVREADERHANDLER_H

#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ScopedPrinter.h"
#include <string>
#include <variant>
#include <vector>

namespace llvm {
namespace logicalview {

/// Ordered collection of owned logical-view readers.
using LVReaders = std::vector<std::unique_ptr<LVReader>>;
/// Command-line argument strings naming input binary files.
using ArgVector = std::vector<std::string>;
/// Variant handle selecting which binary input form to read.
using InputHandle =
    std::variant<StringRef *, MemoryBufferRef *, object::ObjectFile *,
                 object::IRObjectFile *, pdb::PDBFile *>;

/// Creates logical readers for input binaries and prints or compares views.
///
/// This class performs the following tasks:
/// - Creates a logical reader for every binary file in the command line,
///   that parses the debug information and creates a high level logical
///   view representation containing scopes, symbols, types and lines.
/// - Prints and compares the logical views.
///
/// The supported binary formats are: ELF, Mach-O and CodeView.
class LVReaderHandler {
  ArgVector &Objects;
  ScopedPrinter &W;
  raw_ostream &OS;
  LVReaders TheReaders;

  Error createReaders();
  Error printReaders();
  Error compareReaders();

  Error handleArchive(LVReaders &Readers, StringRef Filename,
                      object::Archive &Arch);
  Error handleBuffer(LVReaders &Readers, StringRef Filename,
                     MemoryBufferRef Buffer, StringRef ExePath = {});
  LLVM_ABI Error handleFile(LVReaders &Readers, StringRef Filename,
                            StringRef ExePath = {});
  Error handleMach(LVReaders &Readers, StringRef Filename,
                   object::MachOUniversalBinary &Mach);
  Error handleObject(LVReaders &Readers, StringRef Filename,
                     object::Binary &Binary);
  Error handleObject(LVReaders &Readers, StringRef Filename, StringRef Buffer,
                     StringRef ExePath);
  Error handleObject(LVReaders &Readers, StringRef Filename,
                     MemoryBufferRef Buffer);

  Error createReader(StringRef Filename, LVReaders &Readers, InputHandle &Input,
                     StringRef FileFormatName, StringRef ExePath = {});

public:
  /// Default construction is deleted; input objects and options are required.
  LVReaderHandler() = delete;
  /// Construct a handler for \p Objects using \p W and \p ReaderOptions.
  /// \param Objects Command-line paths of binaries to process.
  /// \param W Printer whose stream receives handler output.
  /// \param ReaderOptions Logical-view options applied to created readers.
  LVReaderHandler(ArgVector &Objects, ScopedPrinter &W,
                  LVOptions &ReaderOptions)
      : Objects(Objects), W(W), OS(W.getOStream()) {
    setOptions(&ReaderOptions);
  }
  /// Copy construction is not allowed.
  /// \param Other Unused source reader handler.
  LVReaderHandler(const LVReaderHandler &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source reader handler.
  LVReaderHandler &operator=(const LVReaderHandler &Other) = delete;

  /// Create readers for \p Filename and append them to \p Readers.
  /// \param Filename Path of the binary file to read.
  /// \param Readers Collection that receives the created readers.
  /// \returns Success or an error describing why reader creation failed.
  Error createReader(StringRef Filename, LVReaders &Readers) {
    return handleFile(Readers, Filename);
  }
  /// Create readers for all configured objects, then print or compare them.
  /// \returns Success or an error describing why processing failed.
  LLVM_ABI Error process();

  /// Create a logical reader for the binary at \p Pathname.
  /// \param Pathname Path of the binary file to read.
  /// \returns The created reader, or an error if creation failed.
  Expected<std::unique_ptr<LVReader>> createReader(StringRef Pathname) {
    LVReaders Readers;
    if (Error Err = createReader(Pathname, Readers))
      return std::move(Err);
    return std::move(Readers[0]);
  }

  /// Print handler state to \p OS.
  /// \param OS Stream that receives the printed handler output.
  LLVM_ABI void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump handler state to the debug stream.
  void dump() const { print(dbgs()); }
#endif
};

} // end namespace logicalview
} // namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVREADERHANDLER_H
