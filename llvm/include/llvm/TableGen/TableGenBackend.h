//===- llvm/TableGen/TableGenBackend.h - Backend utilities ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Useful utilities for TableGen backends.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_TABLEGENBACKEND_H
#define LLVM_TABLEGEN_TABLEGENBACKEND_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/TableGen/Main.h"
#include "llvm/TableGen/Record.h"

namespace llvm {

class RecordKeeper;
class raw_ostream;

/// TableGen backend registration and invocation helpers.
namespace TableGen {
/// Command-line options that register and apply TableGen emitters.
namespace Emitter {

/// Represents the emitting function. Can produce a single or multple output
/// files.
struct FnT {
  /// Function type for an emitter that writes a single output file.
  using SingleFileGeneratorType = void(const RecordKeeper &Records,
                                       raw_ostream &OS);
  /// Function type for an emitter that writes multiple output files.
  using MultiFileGeneratorType = TableGenOutputFiles(
      StringRef FilenamePrefix, const RecordKeeper &Records);

  /// Emitter that writes one file to a stream, or null.
  SingleFileGeneratorType *SingleFileGenerator = nullptr;
  /// Emitter that writes multiple files, or null.
  MultiFileGeneratorType *MultiFileGenerator = nullptr;

  /// Construct an empty emitter with no generator set.
  FnT() = default;
  /// Construct an emitter that writes a single output file.
  ///
  /// \param Gen Single-file generator function.
  FnT(SingleFileGeneratorType *Gen) : SingleFileGenerator(Gen) {}
  /// Construct an emitter that writes multiple output files.
  ///
  /// \param Gen Multi-file generator function.
  FnT(MultiFileGeneratorType *Gen) : MultiFileGenerator(Gen) {}

  /// Return true if both generator pointers match \p Other.
  ///
  /// \param Other Emitter to compare against.
  /// \return True if both generator pointers are equal.
  bool operator==(const FnT &Other) const {
    return SingleFileGenerator == Other.SingleFileGenerator &&
           MultiFileGenerator == Other.MultiFileGenerator;
  }
};

/// Command-line option that selects a TableGen emitter callback.
///
/// Creating an `Opt` object registers the command line option \p Name with
/// TableGen backend and associates the callback \p CB with that option. If
/// \p ByDefault is true, then that callback is applied by default if no
/// command line option was specified.
struct Opt {
  /// Register emitter callback \p CB under command-line option \p Name.
  ///
  /// \param Name Literal option name added to the TableGen action parser.
  /// \param CB Emitter invoked when this option is selected.
  /// \param Desc Help text shown for this option.
  /// \param ByDefault If true, use \p CB when no action option is given.
  LLVM_ABI Opt(StringRef Name, FnT CB, StringRef Desc, bool ByDefault = false);
};

/// Convienence wrapper around `Opt` that registers `EmitterClass::run` as the
/// callback.
template <class EmitterC> class OptClass : Opt {
  static TableGenOutputFiles run(StringRef /*FilenamePrefix*/,
                                 const RecordKeeper &RK) {
    std::string S;
    raw_string_ostream OS(S);
    EmitterC(RK).run(OS);
    return {std::move(S), {}};
  }

public:
  /// Register a single-file emitter class under command-line option \p Name.
  ///
  /// \param Name Literal option name added to the TableGen action parser.
  /// \param Desc Help text shown for this option.
  OptClass(StringRef Name, StringRef Desc) : Opt(Name, run, Desc) {}
};

/// A version of the wrapper for backends emitting multiple files.
template <class EmitterC> class MultiFileOptClass : Opt {
  static TableGenOutputFiles run(StringRef FilenamePrefix,
                                 const RecordKeeper &RK) {
    return EmitterC(RK).run(FilenamePrefix);
  }

public:
  /// Register a multi-file emitter class under command-line option \p Name.
  ///
  /// \param Name Literal option name added to the TableGen action parser.
  /// \param Desc Help text shown for this option.
  MultiFileOptClass(StringRef Name, StringRef Desc) : Opt(Name, run, Desc) {}
};

/// Apply callback for any command line option registered above.
///
/// \param Records TableGen records passed to the selected emitter.
/// \param OutFiles Output files populated by the selected emitter.
/// \param FilenamePrefix Prefix used by multi-file emitters for extra files.
/// \return True if no callback was applied.
LLVM_ABI bool ApplyCallback(const RecordKeeper &Records,
                            TableGenOutputFiles &OutFiles,
                            StringRef FilenamePrefix);

} // namespace Emitter
} // namespace TableGen

/// Output an LLVM style file header to the specified raw_ostream.
///
/// \param Desc Description written into the header banner.
/// \param OS Stream that receives the header.
/// \param Record Record keeper whose input filename is noted when set.
LLVM_ABI void emitSourceFileHeader(StringRef Desc, raw_ostream &OS,
                                   const RecordKeeper &Record = RecordKeeper());

} // namespace llvm

#endif // LLVM_TABLEGEN_TABLEGENBACKEND_H
