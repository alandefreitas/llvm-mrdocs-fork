//===- XCOFFObjcopy.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_XCOFF_XCOFFOBJCOPY_H
#define LLVM_OBJCOPY_XCOFF_XCOFFOBJCOPY_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class Error;
class raw_ostream;

namespace object {
class XCOFFObjectFile;
} // end namespace object

namespace objcopy {
struct CommonConfig;
struct XCOFFConfig;

/// XCOFF-specific object-file copying and stripping operations.
namespace xcoff {
/// Apply the transformations described by \p Config and \p XCOFFConfig
/// to \p In and writes the result into \p Out.
/// \param Config Common objcopy configuration options.
/// \param XCOFFConfig XCOFF-specific configuration options.
/// \param In Input XCOFF object file to transform.
/// \param Out Output stream to write the transformed binary to.
/// \returns any Error encountered whilst performing the operation.
LLVM_ABI Error executeObjcopyOnBinary(const CommonConfig &Config,
                                      const XCOFFConfig &XCOFFConfig,
                                      object::XCOFFObjectFile &In,
                                      raw_ostream &Out);

} // end namespace xcoff
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_OBJCOPY_XCOFF_XCOFFOBJCOPY_H
