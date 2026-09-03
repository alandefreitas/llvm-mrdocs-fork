//===-- ResourceProcessor.h -------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_INCLUDE_LLVM_SUPPORT_WINDOWS_RESOURCE_PROCESSOR_H
#define LLVM_INCLUDE_LLVM_SUPPORT_WINDOWS_RESOURCE_PROCESSOR_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <vector>


namespace llvm {

/// Compiles Windows resource (.rc) script input into a .res binary stream.
///
/// Collects preprocessor defines and include paths, then runs the resource
/// compilation pipeline over the supplied script text and writes the result
/// to an output stream.
class WindowsResourceProcessor {
public:
  /// Path buffer type used for include directories.
  using PathType = SmallVector<char, 64>;

  /// Construct a processor with no defines, includes, or option flags set.
  WindowsResourceProcessor() {}

  /// Add a preprocessor define for the resource script.
  /// @param Key Macro name to define.
  /// @param Value Optional replacement text; empty means define with no value.
  void addDefine(StringRef Key, StringRef Value = StringRef()) {
    PreprocessorDefines.emplace_back(Key, Value);
  }
  /// Add a directory to search when resolving #include directives.
  /// @param IncludePath Directory path to append to the include search list.
  void addInclude(const PathType &IncludePath) {
    IncludeList.push_back(IncludePath);
  }
  /// Enable or disable verbose diagnostic output during processing.
  /// @param Verbose True to emit verbose messages.
  void setVerbose(bool Verbose) { IsVerbose = Verbose; }
  /// Control whether a trailing null is appended to STRINGTABLE entries.
  /// @param NullAtEnd True to append a null terminator to each STRINGTABLE string.
  void setNullAtEnd(bool NullAtEnd) { AppendNull = NullAtEnd; }

  /// Compile the resource script and write a .res binary to \p OutputStream.
  /// @param InputData Contents of the .rc script to process.
  /// @param OutputStream Destination stream that receives the compiled .res.
  /// @return Success, or an error describing why compilation failed.
  Error process(StringRef InputData,
    std::unique_ptr<raw_fd_ostream> OutputStream);

private:
  StringRef InputData;
  std::vector<PathType> IncludeList;
  std::vector<std::pair<StringRef, StringRef>> PreprocessorDefines;
  bool IsVerbose, AppendNull;
};

}

#endif
