//===- WasmObjcopy.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_WASM_WASMOBJCOPY_H
#define LLVM_OBJCOPY_WASM_WASMOBJCOPY_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class Error;
class raw_ostream;

namespace object {
class WasmObjectFile;
} // end namespace object

namespace objcopy {
struct CommonConfig;
struct WasmConfig;

/// Wasm-specific object-file copying and stripping operations.
namespace wasm {
/// Apply the transformations described by \p Config and \p WasmConfig
/// to \p In and writes the result into \p Out.
/// \param Config Common objcopy configuration options.
/// \param WasmConfig Wasm-specific configuration options.
/// \param In Input Wasm object file to transform.
/// \param Out Output stream to write the transformed binary to.
/// \returns any Error encountered whilst performing the operation.
LLVM_ABI Error executeObjcopyOnBinary(const CommonConfig &Config,
                                      const WasmConfig &WasmConfig,
                                      object::WasmObjectFile &In,
                                      raw_ostream &Out);

} // end namespace wasm
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_OBJCOPY_WASM_WASMOBJCOPY_H
