//===-- RISCVISAUtils.h - RISC-V ISA Utilities ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities shared by TableGen and RISCVISAInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RISCVISAUTILS_H
#define LLVM_SUPPORT_RISCVISAUTILS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <map>
#include <string>

namespace llvm {

/// Utilities for RISC-V ISA extension names, versions, and canonical ordering.
namespace RISCVISAUtils {
/// Single-letter standard extensions in canonical order (excluding \c e/\c i).
constexpr StringLiteral AllStdExts = "mafdqlcbkjtpvnh";

/// Represents the major and version number components of a RISC-V extension.
struct ExtensionVersion {
  /// Major version number of the extension.
  unsigned Major;
  /// Minor version number of the extension.
  unsigned Minor;
};

/// Return true if extension name \p LHS sorts before \p RHS in canonical order.
///
/// Compares extension names only; versions are ignored.
///
/// \param LHS Left-hand extension name.
/// \param RHS Right-hand extension name.
/// \return True if \p LHS sorts before \p RHS in canonical order.
LLVM_ABI bool compareExtension(const std::string &LHS, const std::string &RHS);

/// Helper class for OrderedExtensionMap.
struct ExtensionComparator {
  /// Return true if extension name \p LHS sorts before \p RHS in canonical order.
  ///
  /// \param LHS Left-hand extension name.
  /// \param RHS Right-hand extension name.
  /// \return True if \p LHS sorts before \p RHS in canonical order.
  bool operator()(const std::string &LHS, const std::string &RHS) const {
    return compareExtension(LHS, RHS);
  }
};

/// OrderedExtensionMap is std::map, it's specialized to keep entries
/// in canonical order of extension.
using OrderedExtensionMap =
    std::map<std::string, ExtensionVersion, ExtensionComparator>;

} // namespace RISCVISAUtils

} // namespace llvm

#endif
