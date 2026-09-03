//===- LLVMDriver.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_LLVMDRIVER_H
#define LLVM_SUPPORT_LLVMDRIVER_H

namespace llvm {

/// Context describing how an llvm-driver tool was invoked.
struct ToolContext {
  /// Path to the executable used to invoke the tool (\c argv[0]).
  const char *Path;
  /// Argument the llvm-driver prepends when reinvoking this tool.
  const char *PrependArg;
  /// Whether \c PrependArg is required to select the correct tool.
  ///
  /// PrependArg will be added unconditionally by the llvm-driver, but
  /// NeedsPrependArg will be false if Path is adequate to reinvoke the tool.
  /// This is useful if realpath is ever called on Path, in which case it will
  /// point to the llvm-driver executable, where PrependArg will be needed to
  /// invoke the correct tool.
  bool NeedsPrependArg;
};

} // namespace llvm

#endif
