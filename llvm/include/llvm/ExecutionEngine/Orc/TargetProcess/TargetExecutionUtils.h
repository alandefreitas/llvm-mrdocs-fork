//===-- TargetExecutionUtils.h - Utils for execution in target --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for execution in the target process.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_TARGETEXECUTIONUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_TARGETEXECUTIONUTILS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {
namespace orc {

/// Run a main function, returning the result.
///
/// If the optional ProgramName argument is given then it will be inserted
/// before the strings in Args as the first argument to the called function.
///
/// It is legal to have an empty argument list and no program name, however
/// many main functions will expect a name argument at least, and will fail
/// if none is provided.
/// \param Main Function pointer to invoke as a main entry point.
/// \param Args Argument strings passed to Main after any program name.
/// \param ProgramName Optional argv[0] inserted before Args; defaults to none.
/// \return The integer exit code returned by Main.
LLVM_ABI int runAsMain(int (*Main)(int, char *[]), ArrayRef<std::string> Args,
                       std::optional<StringRef> ProgramName = std::nullopt);

/// Run a nullary int-returning function and return its result.
/// \param Func Function pointer with no parameters that returns int.
/// \return The integer value returned by Func.
LLVM_ABI int runAsVoidFunction(int (*Func)(void));

/// Run an int-to-int function with the given argument and return its result.
/// \param Func Function pointer that takes one int and returns int.
/// \param Arg Integer argument passed to Func.
/// \return The integer value returned by Func.
LLVM_ABI int runAsIntFunction(int (*Func)(int), int Arg);

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_TARGETEXECUTIONUTILS_H
