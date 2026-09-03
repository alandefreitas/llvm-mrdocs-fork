//===-- DebugerSupport.h - Utils for enabling debugger support --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for enabling debugger support.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_DEBUGGERSUPPORT_H
#define LLVM_EXECUTIONENGINE_ORC_DEBUGGERSUPPORT_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace orc {

class LLJIT;

/// Enable debugger support for the given LLJIT instance.
///
/// Installs the appropriate ObjectLinkingLayer plugin for the JIT's object
/// format (ELF or MachO) so that debug info can be registered with a debugger.
/// Requires an ObjectLinkingLayer (JITLink); other formats are unsupported.
/// @param J LLJIT instance to enable debugger support for.
/// @return Success, or an error if debugger support cannot be enabled.
LLVM_ABI Error enableDebuggerSupport(LLJIT &J);

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_DEBUGGERSUPPORT_H
