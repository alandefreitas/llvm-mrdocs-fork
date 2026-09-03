//===--- llvm/CodeGen/WasmEHInfo.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Data for Wasm exception handling schemes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_WASMEHINFO_H
#define LLVM_CODEGEN_WASMEHINFO_H

namespace llvm {

/// Constants for WebAssembly exception handling.
namespace WebAssembly {

/// WebAssembly exception tags passed to catch instructions.
enum Tag {
  /// Tag for C++ exceptions (\c __cpp_exception).
  CPP_EXCEPTION = 0,
  /// Tag for C \c longjmp exceptions (\c __c_longjmp).
  C_LONGJMP = 1
};

} // namespace WebAssembly

} // namespace llvm

#endif // LLVM_CODEGEN_WASMEHINFO_H
